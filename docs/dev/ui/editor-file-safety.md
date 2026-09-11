# Editor file publication

The editor must preserve source bytes, avoid silent overwrite of external work,
and recover interrupted saves. `DurableCreateExact` supplies the first native
boundary: publish a complete new file only if its target name is still absent.
It does not yet connect Save As or autosave to an editor window, implement
overwrite/conflict resolution, or accept the editor file requirements.

## Creating a new file

The operation accepts an absolute UTF-8 path under an existing trusted parent
and up to the existing 16 MiB durable-file limit. It does not use VFS searches,
create directories or rewrite source bytes. The caller must choose an authoring
workspace outside runtime staging. Recovery metadata and source must fit their
complete storage budget; callers cannot truncate a document to fit an envelope.

An exclusively created adjacent temporary file receives the complete payload,
checked writes, file synchronization and checked close. Only then does one
native operation publish the destination without replacing an occupant. The
earlier existence check is only an optimization: a competing creator arriving
after that check still wins without losing its bytes.

Windows uses `MoveFileExW` with `MOVEFILE_WRITE_THROUGH`, omitting replacement and
cross-volume copy flags. POSIX uses `linkat` on this operation's own new temporary
file, removes that temporary name, then synchronizes the parent directory. It
never links an existing source document or installed asset. Filesystems lacking
the required operation or durability barriers refuse. The native contracts are
[Microsoft's move semantics](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)
and [POSIX link creation](https://man7.org/linux/man-pages/man2/link.2.html).

The result distinguishes `Created`, `Exists` and `Failed`. A normal collision
clears the diagnostic and leaves the occupant untouched; inaccessible or
nonregular targets can instead fail. A failure after publication may leave the
complete new file visible with durability unconfirmed. Preserve the attempted
source/path and recovery evidence in that case. Never convert a creation retry
into `DurableReplaceExact`, infer ownership from equal bytes, or remove an
unverified target. Concurrent creators require no shared lock to avoid replacing
each other. Later edits and overwrite operations need their own protocol.

The existing trusted-parent, permissions and filesystem limitations remain:
this is not protection against a process replacing directory ancestors or
changing a file after publication. OS durability acknowledgements are not
power-loss experiments or guarantees against faulty hardware.

## Qualification and remaining integration

The standalone durable-file suite runs actual native files, separate processes
and concurrent threads. It checks exact binary/Unicode-path round trips, a
creator between preflight and publication, partial writes, close/sync failures,
uncertain publication, collision retries, limits and invalid targets. Compiled
mutations must reach the assertions; a mutant compile failure is not a pass.
Windows symlink fixtures report a skip when the account lacks that privilege.

The editor still needs native open/reopen with validated source identity,
association with the exact live document revision, atomic overwrite with
external-change preservation, recoverable autosave records, crash/relaunch
reconciliation and localized conflict choices. It must retain both versions
when external work diverges and demonstrate visible edit/undo/save/reopen through
the shared engine canvas. No editor requirement or migration is accepted here.
