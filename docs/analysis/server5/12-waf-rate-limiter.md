# 12. WAF Rate Limiter

**File:** `Core/Network/WAFRateLimiter.cs` (103 lines)

Token bucket per IP: 60 packets/sec + 5 burst. Uses `ConcurrentDictionary<string, TokenBucket>`. Refills tokens based on elapsed time via `Stopwatch.GetTimestamp()`. Dropped packets silently discarded (no disconnect, unlike Server1's aggressive 1000/s disconnect policy).
