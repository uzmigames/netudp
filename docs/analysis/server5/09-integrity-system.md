# 9. Integrity System

**Files:** `Core/Network/Integrity/*.cs`

Client file hash verification (anti-tamper). Server sends random file index → client responds with hash → server compares. 120s timeout. Framework complete but not fully tested.
