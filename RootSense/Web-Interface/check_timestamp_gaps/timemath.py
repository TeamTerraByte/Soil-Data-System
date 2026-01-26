from datetime import datetime, timezone

t1 = datetime(2026, 1, 22, 13, 30, tzinfo=timezone.utc)
t2 = datetime(2026, 1, 24, 7, 30, tzinfo=timezone.utc)

delta = t2 - t1
print(delta)
