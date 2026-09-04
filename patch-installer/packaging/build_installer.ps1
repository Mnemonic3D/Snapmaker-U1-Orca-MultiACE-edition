<#
.SYNOPSIS
    Build the U1 Patch Installer into its standalone, portable .exe.

.DESCRIPTION
    Two stages:
      1. Rebuild payload\manifest.json from sources\patch_sources.json
      2. PyInstaller  ->  dist\<name>.exe   (one file, windowed, portable)

    This produces a single-file executable only. It is never wrapped in a
    Windows-installer-style setup/uninstall package - the installer applies
    patches onto the printer over SSH, it does not install anything onto the
    user's PC, and must stay a standalone/portable .exe that can just be
    downloaded and double-clicked.

.PARAMETER SkipPayload
    Do not rebuild the manifest - package the payload exactly as it stands.

.PARAMETER PythonExe
    Optional path to the Python interpreter used for the build. When omitted,
    the script checks U1_PATCH_PYTHON, a project .venv, per-user Python
    installs, and finally PATH.

.PARAMETER CheckDependencies
    Resolve and verify the build toolchain, print the selected paths and
    versions, then exit without changing the payload or building anything.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File packaging\build_installer.ps1
#>
[CmdletBinding()]
param(
    [ValidateSet('patches', 'enhancements')]
    [string]$Variant = 'patches',
    [switch]$SkipPayload,
    [string]$PythonExe,
    [switch]$CheckDependencies
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Push-Location $Root

# 2026-09-02: this project split into two independent installers that share
# this exact build pipeline - only the spec they package differs. -Variant
# picks which; 'patches' (the default) reproduces the original combined
# installer's identity (same exe name) so existing installs upgrade in place
# instead of appearing as a stranger product.
if ($Variant -eq 'enhancements') {
    $Spec        = Join-Path $Root 'sources\patch_sources_enhancements.json'
    $ExeBaseName = 'MultiACEEnhancementsInstaller'
}
else {
    $Spec        = Join-Path $Root 'sources\patch_sources.json'
    $ExeBaseName = 'MultiACEPatchesInstaller'
}

function Write-Stage($text) {
    Write-Host ""
    Write-Host "=== $text ===" -ForegroundColor Cyan
}

function Resolve-PythonExe {
    $candidates = @()
    if ($PythonExe) { $candidates += $PythonExe }
    if ($env:U1_PATCH_PYTHON) { $candidates += $env:U1_PATCH_PYTHON }
    $candidates += (Join-Path $Root '.venv\Scripts\python.exe')

    # Python installed "for me" is not necessarily placed on PATH. Prefer
    # those real interpreters over the Windows Store execution alias.
    $localPythonRoot = Join-Path $env:LOCALAPPDATA 'Programs\Python'
    if (Test-Path -LiteralPath $localPythonRoot) {
        $localInstalls = Get-ChildItem -LiteralPath $localPythonRoot `
            -Directory -Filter 'Python*' -ErrorAction SilentlyContinue |
            Sort-Object @{Expression={
                $digits = $_.Name -replace '[^0-9]', ''
                if ($digits) { [int]$digits } else { 0 }
            }} -Descending
        foreach ($install in $localInstalls) {
            $candidates += (Join-Path $install.FullName 'python.exe')
        }
    }

    $pathPython = Get-Command python.exe -ErrorAction SilentlyContinue |
                  Select-Object -First 1
    if ($pathPython) { $candidates += $pathPython.Source }

    foreach ($candidate in ($candidates | Where-Object { $_ } |
                            Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            continue
        }
        try {
            # The frozen application is a Tk GUI.  A Python interpreter can
            # be otherwise healthy while its Tcl/Tk installation is absent or
            # broken (as happened with the local Python 3.14 install).  Reject
            # that interpreter before PyInstaller silently omits tkinter and
            # produces an executable that crashes at startup.
            & $candidate -c "import sys, tkinter, PyInstaller, paramiko; root=tkinter.Tk(); root.withdraw(); root.update_idletasks(); root.destroy(); raise SystemExit(0)" 2>$null
            if ($LASTEXITCODE -eq 0) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
        catch {
            # Try the next candidate. The final error lists every location
            # this script knows how to discover.
        }
    }

    throw @"
No usable Python interpreter was found. Install Python 3.9+ or pass:
  -PythonExe C:\path\to\python.exe
The script also honors U1_PATCH_PYTHON and .venv\Scripts\python.exe.
"@
}

try {
    # Resolve the actual build environment before doing any work. A per-user
    # Python install is valid even when the current shell cannot find it on
    # PATH.
    $python = Resolve-PythonExe
    $requirements = Join-Path $Root 'packaging\requirements-build.txt'
    Write-Host "Build Python: $python"

    & $python -c "import PyInstaller" 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "PyInstaller is missing from $python.`nRun:  & '$python' -m pip install -r '$requirements'"
    }
    & $python -c "import paramiko" 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "paramiko is missing from $python.`nRun:  & '$python' -m pip install -r '$requirements'"
    }
    & $python -c "import tkinter; root=tkinter.Tk(); root.withdraw(); root.update_idletasks(); root.destroy()" 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Tkinter/Tcl-Tk is missing or unusable in $python. Select a complete Python build with -PythonExe."
    }

    if ($CheckDependencies) {
        Write-Stage "Build dependency check"
        & $python -c "import sys, tkinter, PyInstaller, paramiko; print('Python      ' + sys.version.split()[0]); print('Tkinter     ' + str(tkinter.TkVersion)); print('PyInstaller ' + PyInstaller.__version__); print('paramiko     ' + paramiko.__version__)"
        Write-Host "Dependency check passed." -ForegroundColor Green
        return
    }

    # payload\ for -Variant patches, payload-enhancements\ for the other -
    # keeps the two variants' bundled files from clobbering each other
    # between builds while both still packagable from the same checkout.
    $payloadDirName = if ($Variant -eq 'enhancements') { 'payload-enhancements' } else { 'payload' }
    $payloadDir = Join-Path $Root $payloadDirName

    # -- 1. payload ------------------------------------------------------
    if (-not $SkipPayload) {
        Write-Stage "Rebuilding patch payload ($Variant)"
        & $python tools\build_payload.py --spec $Spec --out $payloadDir
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "build_payload.py reported problems (exit $LASTEXITCODE)."
            Write-Warning "Fix them, or re-run with -SkipPayload to package anyway."
            if ($LASTEXITCODE -gt 1) { throw "Payload build failed." }
        }
    }

    $manifestPath = Join-Path $payloadDir 'manifest.json'
    if (-not (Test-Path $manifestPath)) {
        throw "No $payloadDirName\manifest.json - run tools\build_payload.py --spec $Spec --out $payloadDir first."
    }
    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
    $version = $manifest.version
    Write-Host "Patch: $($manifest.patch_name)  v$version  ($($manifest.files.Count) file(s))"

    # -- 2. PyInstaller --------------------------------------------------
    Write-Stage "Building $ExeBaseName.exe"
    $pyiArgs = @(
        '-m', 'PyInstaller',
        '--noconfirm', '--clean',
        '--onefile', '--windowed',
        '--name', $ExeBaseName,
        '--distpath', (Join-Path $Root 'dist'),
        '--workpath', (Join-Path $Root 'build\pyinstaller'),
        '--specpath', (Join-Path $Root 'build'),
        '--hidden-import', 'paramiko',
        '--collect-submodules', 'paramiko',
        # Bundle the payload inside the .exe so the single file works when it
        # is just downloaded and double-clicked, always under the fixed
        # "payload" name the script looks for by default - see
        # _find_manifest() / _PAYLOAD_DIR_NAME. A loose payload\ folder next
        # to the .exe still wins if one is present.
        '--add-data', ('{0};payload' -f $payloadDir),
        '--exclude-module', 'pytest',
        'u1_patch_installer.py'
    )
    $iconPath = Join-Path $Root 'packaging\app.ico'
    if (Test-Path $iconPath) { $pyiArgs += @('--icon', $iconPath) }

    & $python @pyiArgs
    if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed." }

    $exePath = Join-Path $Root ('dist\{0}.exe' -f $ExeBaseName)
    if (-not (Test-Path $exePath)) { throw "PyInstaller produced no .exe." }
    $sizeMb = [math]::Round((Get-Item $exePath).Length / 1MB, 1)
    Write-Host "Built dist\$ExeBaseName.exe ($sizeMb MB)" -ForegroundColor Green
}
finally {
    Pop-Location
}
