import re
from datetime import datetime, timezone

# Sept 3, 2026, 9:00 AM Eastern Daylight Time (UTC-4)
TARGET = datetime(2026, 9, 3, 13, 0, 0, tzinfo=timezone.utc)
README = "README.md"


def format_remaining(delta):
    total_minutes = int(delta.total_seconds() // 60)
    hours, minutes = divmod(total_minutes, 60)
    return f"{hours}h {minutes}m"


def main():
    now = datetime.now(timezone.utc)
    remaining = TARGET - now

    if remaining.total_seconds() <= 0:
        block = (
            "🎉 **We're live!** Orca Slicer - MultiACE Edition by "
            "Mnemonic3D has been released."
        )
    else:
        block = (
            f"⏳ **Releasing in {format_remaining(remaining)}** "
            f"(target: Sept 3, 2026, 9:00 AM ET)"
        )

    with open(README, "r", encoding="utf-8") as f:
        content = f.read()

    new_content = re.sub(
        r"<!-- COUNTDOWN:START -->.*?<!-- COUNTDOWN:END -->",
        f"<!-- COUNTDOWN:START -->\n{block}\n<!-- COUNTDOWN:END -->",
        content,
        flags=re.DOTALL,
    )

    if new_content != content:
        with open(README, "w", encoding="utf-8") as f:
            f.write(new_content)


if __name__ == "__main__":
    main()
