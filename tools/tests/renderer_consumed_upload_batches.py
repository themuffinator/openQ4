#!/usr/bin/env python3
"""Actual Vulkan batch admission/fence bodies with counted Vulkan/VMA doubles.

Uses the repository's Vulkan types, without loading a driver or creating a device.
Tests ordered completion, failure ownership and no per-upload fence waits.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <stdexcept>
using VmaAllocation=void*;
using vkImmediateRecord_t=void(*)(VkCommandBuffer,void*);
enum{RDP_WAIT_FAILED,RDP_RECORD_FAILED,RDP_SUBMIT_FAILED};
const int VK_MAX_UPLOAD_BATCH_STAGING=2;
const VkDeviceSize VK_UPLOAD_BATCH_BYTE_BUDGET=64;
struct Context {
 VkDevice device=(VkDevice)1;VkCommandBuffer uploadCommandBuffer=(VkCommandBuffer)2;
 VkQueue graphicsQueue=(VkQueue)3;VkFence uploadFence=(VkFence)4;void* allocator=nullptr;
 bool uploadBatchOpen=false,uploadBatchInFlight=false,presentationBlocked=false;
 uint64_t uploadBatchSerial=0,uploadBatchCompletedSerial=0;
 int numUploadBatchPending=0,numUploadBatchInFlight=0;
 VkDeviceSize uploadBatchPendingBytes=0;
 VkBuffer uploadBatchPendingBuffers[2]{},uploadBatchInFlightBuffers[2]{};
 VmaAllocation uploadBatchPendingAllocations[2]{},uploadBatchInFlightAllocations[2]{};
}vkCtx;
static int checks=0,native=0,waits=0,resets=0,destroys=0,records=0,failure=0;
static uint64_t serial=0;static bool exhausted=false,driftWait=false,driftReset=false,driftDestroy=false;
#define TEST(x)do{++checks;if(!(x))throw std::runtime_error(#x);}while(0)
uint64_t R_ImagePolicyNewResourceIdentity()noexcept{return exhausted?0:++serial;}
void VK_Device_BlockPresentation(int,VkResult,const char*){vkCtx.presentationBlocked=true;}
VkResult vkWaitForFences(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){++native;++waits;if(driftWait)++vkCtx.uploadBatchSerial;return failure==1?VK_ERROR_DEVICE_LOST:VK_SUCCESS;}
VkResult vkResetFences(VkDevice,uint32_t,const VkFence*){++native;++resets;if(driftReset)vkCtx.device=(VkDevice)7;return failure==2?VK_ERROR_DEVICE_LOST:VK_SUCCESS;}
void vmaDestroyBuffer(void*,VkBuffer,VmaAllocation){++native;++destroys;if(driftDestroy)++vkCtx.uploadBatchSerial;}
VkResult vkEndCommandBuffer(VkCommandBuffer){++native;return failure==3?VK_ERROR_DEVICE_LOST:VK_SUCCESS;}
VkResult vkQueueSubmit(VkQueue,uint32_t,const VkSubmitInfo*,VkFence){++native;return failure==4?VK_ERROR_DEVICE_LOST:VK_SUCCESS;}
VkResult vkResetCommandBuffer(VkCommandBuffer,VkCommandBufferResetFlags){++native;return failure==5?VK_ERROR_DEVICE_LOST:VK_SUCCESS;}
VkResult vkBeginCommandBuffer(VkCommandBuffer,const VkCommandBufferBeginInfo*){++native;return failure==6?VK_ERROR_DEVICE_LOST:VK_SUCCESS;}
static void Record(VkCommandBuffer,void*){++records;}
'''
MAIN = r'''
static void Reset(){vkCtx={};native=waits=resets=destroys=records=failure=0;exhausted=driftWait=driftReset=driftDestroy=false;}
static bool Admit(uint64_t& batch,VkDeviceSize bytes=8){return VK_Device_BatchedUpload(Record,nullptr,(VkBuffer)5,(void*)6,bytes,&batch);}
int main(){try{
 Reset();uint64_t a=999,b=998;TEST(Admit(a));TEST(a&&a!=999&&records==1&&waits==0&&vkCtx.uploadBatchOpen);
 TEST(vkCtx.uploadBatchCompletedSerial==0);TEST(Admit(b));TEST(a==b&&waits==0&&records==2);
 TEST(vkCtx.uploadBatchInFlight&&!vkCtx.uploadBatchOpen&&vkCtx.numUploadBatchPending==0&&vkCtx.numUploadBatchInFlight==2);
 TEST(vkCtx.uploadBatchCompletedSerial==0);VK_Device_WaitUploadBatch();
 TEST(vkCtx.uploadBatchCompletedSerial==a&&waits==1&&resets==1&&destroys==2&&!vkCtx.uploadBatchInFlight);
 TEST(Admit(b));TEST(b>a&&vkCtx.uploadBatchCompletedSerial==a&&waits==1);VK_Device_FlushUploadBatch();
 TEST(vkCtx.uploadBatchCompletedSerial==a);VK_Device_WaitUploadBatch();TEST(vkCtx.uploadBatchCompletedSerial==b);
 const int previous=native;VK_Device_WaitUploadBatch();TEST(native==previous);
 for(int f=1;f<=6;++f){Reset();a=991;
  if(f<=4){TEST(Admit(a));failure=f;VK_Device_FlushUploadBatch();if(f<=2)VK_Device_WaitUploadBatch();}
  else {failure=f;TEST(!Admit(a));TEST(a==991&&records==0);}
  TEST(vkCtx.presentationBlocked&&vkCtx.uploadBatchCompletedSerial==0&&destroys==0);
  const int stopped=native;b=992;TEST(!Admit(b)&&b==992);VK_Device_FlushUploadBatch();VK_Device_WaitUploadBatch();TEST(native==stopped);
  if(f==1)TEST(resets==0);if(f==3||f==4)TEST(vkCtx.numUploadBatchPending==1); // Staging remains owned after uncertain submit.
 }
 Reset();TEST(Admit(a,64));TEST(vkCtx.uploadBatchInFlight&&waits==0);failure=1;b=993;TEST(!Admit(b)&&b==993);TEST(records==1&&destroys==0);
 Reset();exhausted=true;a=994;TEST(!Admit(a)&&a==994);TEST(native==0&&records==0&&vkCtx.numUploadBatchPending==0&&vkCtx.presentationBlocked);
 Reset();vkCtx.device=VK_NULL_HANDLE;a=995;TEST(!Admit(a)&&a==995&&native==0);
 Reset();TEST(Admit(a));failure=4;TEST(Admit(b));TEST(b==a&&vkCtx.presentationBlocked&&vkCtx.numUploadBatchPending==2&&destroys==0);
 for(int drift=0;drift<3;++drift){Reset();TEST(Admit(a));VK_Device_FlushUploadBatch();driftWait=drift==0;driftReset=drift==1;driftDestroy=drift==2;
  VK_Device_WaitUploadBatch();TEST(vkCtx.presentationBlocked&&vkCtx.uploadBatchCompletedSerial==0);
  if(drift==0)TEST(resets==0);if(drift<2)TEST(destroys==0);}
 // A forced-flush failure still reports admission: the queue owns staging, so the caller must not free it twice.
 std::printf("actual Vulkan consumed upload batches: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repository', type=Path, default=ROOT)
    parser.add_argument('--engine-repository', type=Path, default=None)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--mutations', action='store_true')
    parser.add_argument('--compiler')
    args = parser.parse_args()
    root = args.repository.resolve()
    engine = (args.engine_repository or root).resolve()
    helper = root/'tools/tests/renderer_consumed_policy.py'
    spec = importlib.util.spec_from_file_location('consumed', helper)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    source = root/'src/renderer/Vulkan/VulkanDevice.cpp'
    header = root/'src/renderer/Vulkan/VulkanDevice.h'
    paths = [source, header, helper, root/'tools/tests/renderer_consumed_upload_batches.py']
    def hashes(): return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    before = hashes()
    text = source.read_text()
    bodies = '\n'.join(module.method(text, sig) for sig in ['void VK_Device_WaitUploadBatch(', 'void VK_Device_FlushUploadBatch(', 'bool VK_Device_BatchedUpload('])
    assert 'uint64_t* acceptedBatch = nullptr' in header.read_text()
    cases = [('actual', bodies, False)]
    if args.mutations:
        changes = [
            ('complete-on-admission', 'if (acceptedBatch) *acceptedBatch = vkCtx.uploadBatchSerial;', 'if (acceptedBatch) *acceptedBatch = vkCtx.uploadBatchSerial; vkCtx.uploadBatchCompletedSerial=vkCtx.uploadBatchSerial;'),
            ('complete-old-batch', 'vkCtx.uploadBatchCompletedSerial = completedBatch;', 'vkCtx.uploadBatchCompletedSerial = 1;'),
            ('skip-wait', 'vkWaitForFences( vkCtx.device, 1, &vkCtx.uploadFence, VK_TRUE, UINT64_MAX )', 'VK_SUCCESS'),
            ('ignore-wait-failure', 'if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_WAIT_FAILED, res, "upload fence wait" ); return; }', '(void)0;'),
            ('missing-marker', 'if (acceptedBatch) *acceptedBatch = vkCtx.uploadBatchSerial;', '(void)acceptedBatch;'),
            ('batch-token-reuse', 'const uint64_t nextBatch = R_ImagePolicyNewResourceIdentity();', 'const uint64_t nextBatch = 1;'),
            ('false-after-owned-flush', '\treturn true;\n}', '\treturn !vkCtx.presentationBlocked;\n}'),
        ]
        for tag, old, new in changes:
            assert old in bodies
            cases.append((tag, bodies.replace(old, new, 1), True))
    compiler = args.compiler or next((p for n in ['clang++', 'g++', 'c++'] if (p := shutil.which(n))), None); assert compiler
    out = Path(tempfile.mkdtemp(prefix='consumed-batches-', dir=root/'.tmp'))
    env = dict(os.environ, TEMP=str(out), TMP=str(out), TMPDIR=str(out))
    records = []; status = 'failed'
    try:
        for tag, body, rejected in cases:
            cpp = out/(tag+'.cpp'); cpp.write_text(SUPPORT+body+MAIN)
            binary = out/(tag+'.exe')
            command = [compiler, '-std=c++20', '-I', str(engine/'src/external/vulkan/include')]
            if args.sanitize: command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-no-pie']
            command += [str(cpp), '-o', str(binary)]
            if Path(compiler).stem.lower() in ['cl', 'clang-cl']:
                if args.sanitize: raise RuntimeError('Use GCC/Clang Unix-style driver for sanitizers')
                command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/MTd', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/I'+str(engine/'src/external/vulkan/include'), str(cpp), '/Fe:'+str(binary), '/Fo:'+str(out/(tag+'.obj'))]
            for stage, call in [('compile', command), ('run', [str(binary)])]:
                result = subprocess.run(call, cwd=out, env=env, timeout=90, capture_output=True, text=True)
                log = out/(tag+'-'+stage+'.log'); log.write_text(result.stdout+result.stderr)
                records.append({'case': tag, 'stage': stage, 'command': call, 'exit_code': result.returncode, 'expected_rejection': stage == 'run' and rejected, 'log': str(log), 'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest()})
                if stage == 'compile' and result.returncode or stage == 'run' and (result.returncode != 0) != rejected: raise RuntimeError(str(log))
                if stage == 'run': print(tag+': '+('rejected compiled mutation' if rejected else result.stdout.strip()))
        status = 'passed'
    finally:
        after = hashes()
        if before != after: status = 'source_changed'
        generated = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.suffix in ['.cpp', '.exe']}
        (out/'result.json').write_text(json.dumps({'status': status, 'files_before': before, 'files_after': after, 'records': records, 'generated': generated, 'limitations': __doc__}, indent=2)+'\n')
        print(out/'result.json')
    if status != 'passed': raise SystemExit(1)


if __name__ == '__main__':
    main()
