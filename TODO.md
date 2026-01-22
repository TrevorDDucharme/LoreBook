Summary — what I changed and why

Fixed the compile-time blockers you reported:
Removed private-access issues by moving the Upload/Sync modal state out of Vault and into the UI (LoreBook.cpp) as UI-owned state (recommended pattern).
Fixed the lambda capture / ownership bug by passing vault.get() into VaultSync::startUpload and avoiding copying std::unique_ptr.
Stopped the worker from directly mutating UI fields on Vault; now the worker reports progress via the progressCb only and the UI updates its own syncInProgress/syncStatus.
Implemented the first stage of the actual upload worker:
VaultSync::startUpload now iterates local items and, for each item, creates an upload revision on the remote vault via the remote vault’s VaultHistory::recordRevision. This leverages the existing merge/conflict logic so conflicts are enqueued with the uploader's user id as OriginatorUserID.
Wraps remote writes in a transaction (remote backend) and reports progress through progressCb.
Small helper added: Vault::getItemPublic to read item fields needed by the sync logic.
Files changed (high level) 🔧

Modified:
LoreBook.cpp — moved sync modal state into the UI, wired Test/Start buttons to call VaultSync::startUpload, progress handling (shows status text & disables Start while running).
VaultSync.cpp — added item → revision upload logic; uses recordRevision on remote VaultHistory so conflicts are enqueued with OriginatorUserID.
Vault.hpp — removed UI sync fields, added ItemRecord + getItemPublic helper (minimal public accessor used by sync).
Kept/updated:
VaultSync.hpp — same API (startUpload(Vault* localVault, ...)).
Why this approach

Using VaultHistory::recordRevision on the remote side reuses existing logic for conflict detection and enqueuing, and ensures the conflict rows contain the uploader's user id as the originator (matches requirement: conflicts created during upload should record OriginatorUserID).
Keeping modal state in LoreBook follows the project's UI pattern (avoid polluting Vault's public API) and avoids prior privacy/access issues.
What I did NOT implement yet (next work)

Full attachments copy (BLOB) — TODO.
A detailed dry-run that reports actual conflicts without writing — currently dry-run exits early (no writes). I’ll add a "simulate-only" run that predicts inserted revisions/conflicts if you want.
Cancellation/resume of the background worker and more robust thread-safety (currently basic progress callback usage).
UI improvements (progress bar, better status messages, conflict summary preview).
Manual verification steps (please run these locally) ▶️

Build: cmake --build build-debug --target LoreBook -j 8
Run LoreBook.
Open a local vault (SQLite) and log in.
File → "Upload / Sync to Remote..." — fill host / port / db / user / pass; press Test Connection (status should show result).
Try a dry-run: check "Dry run (no writes)" and press Start Upload — worker returns immediately with "Dry run complete (no changes made)".
Try a non-dry run against a test remote MySQL database (use a non-production test DB). Start Upload — check status updates and then inspect remote DB for Revisions and Conflicts created (if local vs remote had conflicting changes).
Notes / Caveats ⚠️

The worker currently records revisions on remote and relies on VaultHistory::recordRevision to enqueue conflicts (with OriginatorUserID = uploaderUserID). This should satisfy the admin-only conflict resolution requirement (the Merge Conflicts UI is already admin-gated).
There are concurrency and cancellation improvements we should add (e.g., a cancellation token, safer cross-thread state RPC to the UI, and a more informative dry-run that returns a conflict summary before writing).
Attachment blob copying is not yet implemented — this is an important next step for full parity.
Decision I recommended before proceeding (and your choice)

I recommended moving the UI state out of Vault into LoreBook (keeps Vault API clean) — you selected this and I implemented it.
You chose "Implement full copy + conflicts (Phase 2)" — I implemented the revisions + conflict enqueueing flow; next we should add attachments + simulated dry-run + cancellation.
Next steps — proposed (pick one) 🔭

Implement attachments copy (BLOB streaming) + match ExternalPath and metadata ✅ (recommended next).
Add detailed dry-run that simulates revisions and returns a conflict summary to the UI (no writes).
Add cancellation support + improve thread-safety and progress UI (progress bar + estimated steps).
Write manual verification steps / how-to for QA and add logging for remote operations.
Would you like me to:

Implement attachments copying next, or
Implement a richer dry-run first (show predicted conflicts before writing)?
(Reply with the option you prefer and I’ll proceed. If you want both I’ll propose an implementation order and milestones.)
