"""Compile checkpoint commit, recovery, and pending-save polling from production."""
from pathlib import Path
import sys
source=Path(sys.argv[1]).read_text(encoding='utf-8')
groups=[('static inline bool Session_MoveSaveFileDurably(','struct sessionSaveCommitFileOps_t {','struct sessionSaveCommitJob_t {','static void Session_CommitCheckpointFiles(','static bool Session_QueueCheckpointCommit('),('struct sessionRecoverSaveFile_t {','static void Session_InitRecoverSaveFile(','static bool Session_RecoverInterruptedSaveFiles('),('bool idSessionLocal::FinishPendingSave(', 'class sessionSaveOperationGuard_t {')]
for names,out in zip(groups,sys.argv[2:]):
 blocks=[]
 for sig in names:
  start=source.index(sig);end=source.index('{',start)+1;depth=1
  while depth:
   depth+=(source[end]=='{')-(source[end]=='}');end+=1
  if source[end:end+1]==';':end+=1
  blocks.append(source[start:end])
 Path(out).write_text('\n\n'.join(blocks)+'\n',encoding='utf-8')
