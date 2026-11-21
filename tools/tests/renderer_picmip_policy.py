#!/usr/bin/env python3
"""Pins the diffuse-only picmip contract across the image pipeline.

openQ4 reduces textures in three separate loaders (raw 2D, cube, precompressed
DDS) and caches the result in generated .bimage files. A picmip that is applied
in one loader but not another, or that is not folded into the cache key, shows
up as a texture stuck at the wrong size long after the cvar changed. These
checks keep the single-policy shape that prevents that.
"""

from pathlib import Path


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def read_repo_file(relative_path):
    return (Path(__file__).resolve().parents[2] / relative_path).read_text(encoding="utf-8")


IMAGE_H = Path("src") / "renderer" / "Image.h"
CONSUMED_POLICY_H = Path("src") / "renderer" / "RendererConsumedPolicy.h"
IMAGE_MANAGER_CPP = Path("src") / "renderer" / "ImageManager.cpp"
IMAGE_LOAD_CPP = Path("src") / "renderer" / "Image_load.cpp"
IMAGE_PROCESS_CPP = Path("src") / "imagetools" / "Image_process.cpp"
IMAGE_FILES_CPP = Path("src") / "imagetools" / "Image_files.cpp"
RENDER_SYSTEM_CPP = Path("src") / "renderer" / "RenderSystem.cpp"
RENDER_SYSTEM_INIT_CPP = Path("src") / "renderer" / "RenderSystem_init.cpp"


def test_picmip_cvars_are_declared_with_explicit_ranges():
    image_manager = read_repo_file(IMAGE_MANAGER_CPP)

    for snippet in (
        '"image_picmip",',
        '"reduce the diffuse layer of materials by this many mip levels; 0 = full resolution, each step halves the texture. Normal, specular, light, sky, font, and 2D images keep their authored size",',
        "MAX_IMAGE_PICMIP );",
        '"image_picmipFilter",',
        "PICMIP_FILTER_MASK );",
        '"image_picmipMinSize",',
        '"image_picmip stops reducing a texture once its longest axis reaches this size",',
    ):
        assert_true(snippet in image_manager, f"missing picmip cvar detail {snippet!r}")

    assert_true(
        '"1",\n\tCVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,\n\t"image paths image_picmip may reduce' in image_manager,
        "image_picmipFilter should default to the textures/* bucket like the Quake 3 style filter it mirrors",
    )
    assert_true(
        '"32",\n\tCVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER' in image_manager,
        "image_picmipMinSize should default to a 32 pixel floor",
    )


def test_picmip_filter_buckets_are_shared_constants():
    image_h = read_repo_file(IMAGE_H)

    for snippet in (
        "static const int	MAX_IMAGE_PICMIP = 16;",
        "static const int	PICMIP_FILTER_ALL = 0;",
        "static const int	PICMIP_FILTER_TEXTURES = BIT( 0 );",
        "static const int	PICMIP_FILTER_MODELS = BIT( 1 );",
        "static const int	PICMIP_FILTER_OTHER = BIT( 2 );",
        "static const int	PICMIP_FILTER_MASK = PICMIP_FILTER_TEXTURES | PICMIP_FILTER_MODELS | PICMIP_FILTER_OTHER;",
    ):
        assert_true(snippet in image_h, f"missing picmip filter constant {snippet!r}")


def test_picmip_only_reduces_the_diffuse_layer():
    image_manager = read_repo_file(IMAGE_MANAGER_CPP)

    assert_true(
        "if ( usage == TD_DIFFUSE && R_ImagePicmipFilterAllows( name, inputs.picmipFilter ) ) {" in image_manager,
        "picmip must be gated on TD_DIFFUSE so bump, specular, light, sky, font, and 2D images keep their authored size",
    )
    assert_true(
        "policy.mipShift = Max( 0, inputs.picmip );" in image_manager,
        "the diffuse gate should be the only thing that sets a mip shift",
    )
    assert_true(
        "\tif ( !allowDownSize ) {\n\t\treturn;\n\t}" in image_manager,
        "'nopicmip' materials and presentation namespaces must opt out before any reduction is chosen",
    )


def test_picmip_filter_classifies_the_logical_image_name():
    image_manager = read_repo_file(IMAGE_MANAGER_CPP)

    assert_true("static bool R_ImagePicmipFilterAllows( const char *name, int value )" in image_manager, "picmip should have a path filter")
    for snippet in (
        'if ( R_ImagePathStartsWith( path, "textures", 8 ) ) {',
        "return ( filter & PICMIP_FILTER_TEXTURES ) != 0;",
        'if ( R_ImagePathStartsWith( path, "models", 6 ) ) {',
        "return ( filter & PICMIP_FILTER_MODELS ) != 0;",
        "return ( filter & PICMIP_FILTER_OTHER ) != 0;",
    ):
        assert_true(snippet in image_manager, f"missing picmip filter bucket {snippet!r}")
    assert_true(
        "static bool R_IsImageProgramNameChar( char c ) {" in image_manager,
        "image programs wrap their subject in a function call, so the filter must unwrap it",
    )
    assert_true(
        "isalnum(" not in image_manager,
        "classification must not use isalnum, which is undefined for high bytes under a signed char",
    )
    assert_true(
        "if ( probe == path || *probe != '(' ) {" in image_manager,
        "only a leading function call may be unwrapped; scanning to the last '(' or ',' lands on a numeric argument",
    )
    assert_true(
        "if ( *scan == '(' || *scan == ',' ) {" not in image_manager,
        "the scan-to-last-separator classification misfiled scale(...)/downsize(...) images and must not return",
    )


def test_one_policy_drives_every_loader_path():
    image_h = read_repo_file(IMAGE_H)
    image_load = read_repo_file(IMAGE_LOAD_CPP)
    image_files = read_repo_file(IMAGE_FILES_CPP)

    policy_h = read_repo_file(CONSUMED_POLICY_H)
    assert_true('#include "RendererConsumedPolicy.h"' in image_h, "image consumers must include the shared contract")
    assert_true("struct imageDownsizePolicy_t {" in policy_h, "the reduction contract should be one shared struct")
    for member in ("maxDimension = 0", "mipShift = 0", "minDimension = 1"):
        assert_true(member in policy_h, f"missing policy member {member!r}")
    assert_true(
        "void R_GetImageDownsizePolicy( const char *name, textureUsage_t usage, bool allowDownSize, imageDownsizePolicy_t &policy );" in image_h,
        "the policy builder should be shared, not duplicated per loader",
    )

    assert_true(
        "bool R_LoadPrecompressedDDS(const char* name, idBinaryImage& image, ID_TIME_T* timestamp, textureUsage_t usage, const imageDownsizePolicy_t& downsizePolicy, bool useMipmaps, imageReductionResult_t* reduction = NULL, const imageFileContent_t* expected = NULL);" in image_h,
        "the DDS loader must take the full policy, not just a size ceiling",
    )
    assert_true(
        "if (!R_ResolveImageReduction(downsizePolicy, info.width, info.height, info.numLevels, selected)) break;" in image_files,
        "the DDS loader should select a mip level instead of decompressing and resampling",
    )

    assert_true("const imageDownsizePolicy_t& consumedDownsize = consumedLoad.Policy();" in image_load,
                "all loader branches must use the one captured per-load value")
    assert_true(image_load.count("if ( consumed ) policy = *consumed;") == 2 and "if (consumed) policy = *consumed;" in image_load,
                "cache-key and raw/cube helpers must prefer the immutable supplied value")
    for call in (
        "precompressedDownsizePolicy = consumedDownsize;",
        "R_DownsizeLoadedImageData( GetName(), usage, allowDownSize, pic, width, height, &consumedDownsize );",
        "R_DownsizeLoadedCubeImageData( GetName(), usage, allowDownSize, pics, size, &consumedDownsize );",
    ):
        assert_true(call in image_load, f"loader path not routed through the shared policy: {call!r}")


def test_ceiling_is_applied_before_the_relative_reduction():
    image_process = read_repo_file(IMAGE_PROCESS_CPP)

    assert_true(
        "void R_ApplyImageDownsizePolicy( const imageDownsizePolicy_t &policy, int &width, int &height ) {" in image_process,
        "the reduction arithmetic should live in one shared function",
    )
    ceiling_at = image_process.find("if ( policy.maxDimension > 0 ) {")
    shift_at = image_process.find("for ( int i = 0; i < policy.mipShift; i++ ) {")
    assert_true(ceiling_at != -1 and shift_at != -1, "policy should apply both a ceiling and a mip shift")
    assert_true(
        ceiling_at < shift_at,
        "the ceiling must be applied first, otherwise a low downSizeLimit silently swallows the first picmip steps",
    )
    assert_true(
        "if ( Max( nextWidth, nextHeight ) < minDimension ) {" in image_process,
        "the picmip floor must be tested against the result of a step, not the size going into it",
    )
    assert_true(
        "int R_ImageDownsizePolicyMipSkip( const imageDownsizePolicy_t &policy, int width, int height, int availableLevels ) {" in image_process,
        "mip-chain selection should be derived from the same policy arithmetic",
    )


def test_generated_cache_key_tracks_the_active_reduction():
    image_h = read_repo_file(IMAGE_H)
    image_load = read_repo_file(IMAGE_LOAD_CPP)

    assert_true(
        "static void			GetGeneratedName(idStr& _name, const char* _policyName, const textureUsage_t& _usage, const cubeFiles_t& _cube, bool allowDownSize = true, unsigned int flags = 0, const imageDownsizePolicy_t* consumed = NULL);" in image_h,
        "the cache key must be told which logical name the policy was classified against",
    )
    assert_true(
        image_load.count("GetGeneratedName( generatedName, GetName(), usage, cubeFiles, allowDownSize, flags, &consumedDownsize );") == 2,
        "both generated-name call sites must classify against the logical image name, including the DDS replacement site",
    )

    assert_true(
        "static unsigned int R_GetImageDownsizeSignature( const char *name, textureUsage_t usage, bool allowDownSize, const imageDownsizePolicy_t* consumed ) {" in image_load,
        "the cache signature should be derived from the shared policy",
    )
    # The trailing byte is a revision of the reduction arithmetic itself. Bump it
    # here and in Image_load.cpp together whenever the pixels a given policy
    # produces change, or players keep serving stale sizes out of their cache.
    assert_true(
        "unsigned int signature = ( static_cast<unsigned int>( policy.maxDimension ) << 8 ) ^ static_cast<unsigned int>( usage ) ^ 0x6F713402u;" in image_load,
        "the reduction revision byte must match the current shrink filter",
    )
    for snippet in (
        "if ( policy.mipShift > 0 ) {",
        "signature ^= ( static_cast<unsigned int>( policy.mipShift ) * 0x9E3779B9u );",
        "signature ^= ( static_cast<unsigned int>( policy.minDimension ) * 0x85EBCA6Bu );",
    ):
        assert_true(snippet in image_load, f"picmip must contribute to the cache key: {snippet!r}")


def test_exact_halvings_reuse_the_mip_chain_filter():
    image_load = read_repo_file(IMAGE_LOAD_CPP)

    assert_true(
        "static int R_CountExactHalvings( int width, int height, int scaledWidth, int scaledHeight ) {" in image_load,
        "the shrink path should detect an exact power-of-two reduction",
    )
    assert_true(
        "byte *next = srgbMips ? R_MipMapWithSRGB( source, level, levelHeight )" in image_load
        and "gammaMips ? R_MipMapWithGamma( source, level, levelHeight ) : R_MipMap( source, level, levelHeight )" in image_load,
        "an exact reduction should use the same filter that builds the mip chain, so image_picmip N matches mip level N",
    )
    assert_true(
        "static bool R_ImageUsageUsesGammaMips( textureUsage_t usage ) {" in image_load,
        "the shrink filter should follow the same gamma choice DeriveOpts makes",
    )
    assert_true(
        "return usage == TD_FONT || usage == TD_LIGHT || usage == TD_PBR_COLOR;" in image_load,
        "gamma mips are used for font, light, and explicitly color-managed PBR buckets",
    )
    assert_true(
        image_load.count("return R_ResampleTexture( pic, width, height, scaledWidth, scaledHeight );") == 1 and "if (!next) return NULL;" in image_load,
        "only non power-of-two reductions resample; failed exact mip chains preserve original input",
    )


def test_reduction_changes_reload_images_without_a_restart():
    image_h = read_repo_file(IMAGE_H)
    image_manager = read_repo_file(IMAGE_MANAGER_CPP)
    render_system = read_repo_file(RENDER_SYSTEM_CPP)

    assert_true("void				CheckCvars();" in image_h, "the image manager should expose a per-frame cvar check")
    assert_true("void idImageManager::CheckCvars() {" in image_manager, "the image manager should implement the cvar check")
    assert_true(
        "globalImages->CheckCvars();" in render_system and "//globalImages->CheckCvars();" not in render_system,
        "R_CheckCvars must actually call the image cvar check",
    )
    assert_true(
        "static bool R_ImageReductionCvarsChanged( bool clear ) {" in image_manager,
        "one shared list should drive both the startup priming and the per-frame check",
    )
    for cvar in (
        "image_picmip",
        "image_picmipFilter",
        "image_picmipMinSize",
        "image_downSize",
        "image_downSizeLimit",
        "image_downSizeSpecular",
        "image_downSizeSpecularLimit",
        "image_downSizeBump",
        "image_downSizeBumpLimit",
    ):
        assert_true(f"&{cvar}" in image_manager, f"{cvar} changes should trigger an image reload")
    def body(source, signature):
        text = source[source.index(signature):]
        return text[:text.index("\n}\n")]

    check = body(image_manager, "void idImageManager::CheckCvars() {")
    apply = body(image_manager, "void idImageManager::ApplyPendingImageCvars( bool finishReloads ) {")
    assert_true(
        "ApplyPendingImageCvars( false );" in check and "ReloadImages(" not in check + apply
        and "ReloadReductionChangedImages();" in apply,
        "a reduction change should reload only the images it reaches, not the whole catalogue",
    )
    assert_true(
        "PendingReductionReloads() <= 0" in check,
        "the per-frame check keeps running while queued images remain, with no cvar change",
    )
    assert_true(
        "ContinueReductionReloads( finishReloads ? 0.0 : static_cast<double>( Max( 0, image_reloadFrameMsec.GetInteger() ) ) );" in apply,
        "queued images reload within image_reloadFrameMsec a frame, or all at once when finishing",
    )
    needs = body(image_manager, "static bool R_ImageNeedsReductionReload( idImage *image ) {")
    assert_true(
        "!image->IsFileBacked() || R_IsMutableRenderImage( image ) || !image->IsLoaded()" in needs
        and "!R_ImagePolicyShouldReload( image )" in needs
        and "image->ReductionInputsChanged() || R_ImageHeldBelowRequest( image )" in needs,
        "only resident file-backed images whose key moved, or that Vulkan held below their request, are reloaded",
    )
    queue = body(image_manager, "int idImageManager::ReloadReductionChangedImages() {")
    assert_true(
        "R_ImageNeedsReductionReload( image )" in queue and "ClearReductionReloads();" in queue
        and "->Reload(" not in queue,
        "a change builds a fresh queue from every image and reloads nothing itself",
    )
    step = body(image_manager, "bool idImageManager::ContinueReductionReloads( double budgetMsec ) {")
    assert_true(
        step.index("workThisFrame > 0 && !finishing && R_ReductionReloadClockMsec() - frameStart >= budgetMsec")
        < step.index("if ( !R_ImageNeedsReductionReload( image ) ) {")
        < step.index("deferred = image->ReloadOrDefer( job );"),
        "each frame does at least one step, stops at its budget, and checks every image again at its turn",
    )
    assert_true(
        "R_RendererDeviceAwaitsRestart()" in step and "ClearReductionReloads();" in step,
        "a failed device drops the queue: its restart reloads everything",
    )
    for signature in ("void idImageManager::PurgeAllImages() {", "void idImageManager::Shutdown() {"):
        assert_true("ClearReductionReloads();" in body(image_manager, signature),
                    f"{signature} must drop the queue of image pointers")
    end_load = body(image_manager, "void idImageManager::EndLevelLoad() {")
    assert_true(
        end_load.index("LoadLevelImages( true );") < end_load.index("ApplyPendingImageCvars( true );"),
        "a change pending over a level load is finished behind its loading screen",
    )
    image_load = read_repo_file(Path("src/renderer/Image_load.cpp"))
    load = image_load[image_load.index("void idImage::ActuallyLoadImage( bool fromBackEnd ) {"):]
    assert_true(
        load.index("imageConsumedLoad_t consumedLoad(*this);") < load.index("loadedReductionKey = CurrentReductionKey();")
        < load.index("loadedReductionKey = reductionKey_t();"),
        "every file-backed load records its key; a prepared recovery candidate leaves it unknown",
    )
    key = image_load[image_load.index("idImage::reductionKey_t idImage::CurrentReductionKey() const {"):]
    key = key[:key.index("\n}\n")]
    for snippet in ("R_GetImageDownsizePolicy( GetName(), usage, allowDownSize, key.policy );",
                    "R_DecodedImageCompression( usage, cubeFiles, allowDownSize, GetName(), color );",
                    "image_usePrecompressedTextures.GetInteger();"):
        assert_true(snippet in key, f"the reduction key should hold {snippet}")
    assert_true(
        "void idImageManager::ReloadImages( bool all, bool fileBackedOnly ) {" in image_manager,
        "a reload should be able to restrict itself to images that have a file behind them",
    )
    assert_true(
        "if ( fileBackedOnly && R_IsMutableRenderImage( image ) ) {" in image_manager,
        "scratch pages and render targets have nothing to re-reduce, and reloading one "
        "reallocates it with no texels - which empties the generated glyph atlases",
    )
    assert_true(
        check.index("if ( insideLevelLoad || R_ImagePolicyActive() ) {")
        < check.index("ApplyPendingImageCvars( false );")
        and "R_ImageReductionCvarsChanged( true );" not in check,
        "a change during a level load or a checked attempt stays pending, flags still set, "
        "so the images the load kept resident are reloaded after it",
    )

    # Every cvar is born CVAR_MODIFIED, so without a startup prime the first
    # rendered frame of every launch would reload every image for no reason.
    assert_true("void idImageManager::PrimeCvars() {" in image_manager, "startup should swallow the born-modified state")
    assert_true("\tPrimeCvars();" in image_manager, "idImageManager::Init should prime the reduction cvars")
    assert_true("void				PrimeCvars();" in image_h, "PrimeCvars should be part of the image manager interface")

    # A device start rebuilds every image under the settings in effect, so it
    # swallows the modified state as well. Without that, the frame after a
    # restart that follows a change (a Performance Preset's Apply, vid_restart,
    # a launch whose configuration sets a reduction) reloads every image again.
    render_init = read_repo_file(RENDER_SYSTEM_INIT_CPP)
    start = render_init.index("static bool R_InitRendererDevice( bool legacyPolicy, bool forceWindow, char *error, int errorSize ) {")
    device = render_init[start:render_init.index("void idRenderSystemLocal::InitOpenGL( void ) {", start)]
    assert_true(
        device.count("globalImages->ReloadImages") == 2 and device.count("globalImages->PrimeCvars();") == 1,
        "a device start should reload the images on each backend and prime the image cvars once",
    )
    assert_true(
        device.rindex("globalImages->ReloadImages") < device.index("#endif\n\t\t// Every image the device holds")
        < device.index("globalImages->PrimeCvars();") < device.index("r_initialRendererDevicePending = false;"),
        "the prime follows both backends' reload, inside the block that started the device",
    )


def test_picmip_is_documented_for_players():
    display_settings = read_repo_file(Path("docs/user") / "display-settings.md")

    assert_true("## Texture quality and memory" in display_settings, "picmip should have a user-facing section")
    for snippet in (
        "`image_picmip`",
        "`image_picmipFilter`",
        "`image_picmipMinSize`",
        "diffuse layer of materials",
        "A material that declares `nopicmip` opts out of both.",
    ):
        assert_true(snippet in display_settings, f"missing picmip documentation detail {snippet!r}")


def test_reload_decodes_on_workers_from_recorded_reads():
    """The settings reload decodes 2D sources on image workers from file reads the main thread recorded."""

    def body(source, signature):
        text = source[source.index(signature):]
        return text[:text.index("\n}\n")]

    tools_h = read_repo_file(Path("src") / "imagetools" / "ImageTools.h")
    files = read_repo_file(IMAGE_FILES_CPP)
    program = read_repo_file(Path("src") / "imagetools" / "Image_program.cpp")
    binary = read_repo_file(Path("src") / "imagetools" / "BinaryImage.cpp")
    image_h = read_repo_file(IMAGE_H)
    image_load = read_repo_file(IMAGE_LOAD_CPP)
    image_manager = read_repo_file(IMAGE_MANAGER_CPP)
    tr_main = read_repo_file(Path("src") / "renderer" / "tr_main.cpp")

    # The tape: per thread, a replay never reaches the file system, a miss is recorded as a failure.
    assert_true("static thread_local idImageFileTape *imageFileTape = NULL;" in files, "the tape is per thread")
    read = body(files, "static int R_ReadImageSourceFile( const char *name, void **buffer, ID_TIME_T *timestamp ) {")
    replay = read[read.index("IMAGE_TAPE_REPLAY"):read.index("void *bytes = NULL;")]
    assert_true("fileSystem" not in replay and "imageFileTape->missed = true;" in replay,
                "a replayed read is answered from the tape, and a read it does not hold fails the replay")
    assert_true(read.index("void *bytes = NULL;") < read.index("fileSystem->ReadFileContentStamp( name, &bytes, &stamp );"),
                "a recording read takes the bytes even when its caller wants only the timestamp")
    for signature in ("static void LoadTGA( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp ) {",
                      "static void LoadPNG( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp ) {",
                      "static void LoadDDS( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool decodeRXGBNormalMap ) {"):
        loader = body(files, signature)
        assert_true("fileSystem->" not in loader and "R_ReadImageSourceFile(" in loader,
                    f"{signature} reads through the tape helpers")
    assert_true("if ( imageFileTape != NULL ) {" in body(files, "static void LoadJPG( const char *filename, unsigned char **pic, int *width, int *height, ID_TIME_T *timestamp ) {"),
                "LoadJPG reads through the tape too")
    resolve = body(files, "bool R_ResolvePreferredDDSImageSource( const char *cname, idStr &ddsName, ID_TIME_T *timestamp, bool allowPrecompressedDDS, bool *precompressedDDS, bool *retailResultSuperseded ) {")
    assert_true("FindResolve( key, allowPrecompressedDDS )" in resolve and "R_ResolvePreferredDDSImageSourceUntaped(" in resolve,
                "the dds/ replacement lookup is recorded and replayed, never probed on a worker")
    assert_true("if ( R_ImageFileTapeRecording() ) {\n\t\t\t\tR_ReadImageSourceFile( preferredDDSName.c_str(), NULL, NULL );" in files,
                "a timestamp-only probe records the replacement DDS the replay decodes")
    assert_true("static thread_local char parseBuffer[MAX_IMAGE_NAME];" in program, "image programs parse into a per-thread buffer")
    assert_true("class idImageFileTape {" in tools_h and "bool	R_ImageFileTapeReplaying();" in tools_h, "the tape is part of imagetools")
    assert_true("!R_ImageFileTapeReplaying()" in binary, "a worker's block compression runs in its own thread, not in bands")
    assert_true(tr_main.count("if ( R_ImageFileTapeReplaying() ) { return; }") == 2, "workers leave the frame counters alone")

    # The load: defer at the 2D decode, publish only a result that still fits.
    load = body(image_load, "void idImage::ActuallyLoadImage( bool fromBackEnd ) {")
    defer = load.index("route->mode == IMAGE_DECODE_DEFER")
    assert_true(defer < load.index("route->deferred = true;\n\t\t\t\t\treturn;")
                < load.index("R_LoadImageProgramForDeclaredUsage( fallbackLoadSourceName, &pic, &width, &height, &sourceFileTime, usage );"),
                "a deferral records the decode's reads and stops before decoding")
    publish = load[load.index("route->mode == IMAGE_DECODE_PUBLISH && route->job != NULL"):load.index("if ( !workerResult ) {")]
    for condition in ("route->job->sourceName.Cmp( fallbackLoadSourceName ) == 0",
                      "route->job->generatedName.Cmp( generatedName.c_str() ) == 0",
                      "route->job->usage == usage", "R_SameDownsizePolicy( route->job->policy, consumedDownsize )",
                      "R_SameDecodeOpts( route->job->opts, opts )", "usage = job.resultUsage;", "im.SwapContent( *job.image );"):
        assert_true(condition in publish, f"a published result must satisfy {condition}")
    assert_true("if ( bypassGeneratedFile || publishingWorkerResult ) {" in load,
                "publishing skips the cache reads the deferral just found missing")
    reload = body(image_load, "bool idImage::ReloadOrDefer( std::shared_ptr<imageDecodeJob_t> &job ) {")
    assert_true("consumedPolicy = savedConsumedPolicy;" in reload and "usage = savedUsage;" in reload
                and "if ( storageGeneration == storageBefore ) {\n\t\tPurgeImage();" in reload,
                "a deferral leaves the image as it was; a load without storage purges as Reload does")
    worker = body(image_load, "bool R_RunImageDecodeJob( imageDecodeJob_t &job ) {")
    assert_true("idScopedImageFileTape replay( &job.tape, IMAGE_TAPE_REPLAY );" in worker and "catch ( ... )" in worker,
                "the worker decodes from the replayed tape and fails the job on any exception")

    # The workers and the queue.
    assert_true('idCVar image_reloadWorkers( "image_reloadWorkers", "2"' in image_manager, "two workers by default")
    pool = image_manager[image_manager.index("class idImageDecodeWorkers {"):image_manager.index("static idImageDecodeWorkers imageDecodeWorkers;")]
    assert_true("_mm_setcsr( controlWord );" in pool and "R_LowerImageWorkerPriority();" in pool,
                "workers round as the main thread does and run below it")
    assert_true("worker.detach();" in pool[:pool.index("void EnsureStarted")], "a static destructor never joins under the loader lock")
    for signature in ("void idImageManager::PurgeAllImages() {", "void idImageManager::Shutdown() {"):
        assert_true("R_StopImageDecodeWorkers();" in body(image_manager, signature), f"{signature} stops the workers")
    step = body(image_manager, "bool idImageManager::ContinueReductionReloads( double budgetMsec ) {")
    assert_true(step.index("reductionInFlight.front()") < step.index("image->ReloadOrDefer( job )"),
                "each frame publishes finished decodes, oldest first, before it starts new ones")
    assert_true("imageDecodeWorkers.WaitFor(" in step and "imageDecodeWorkers.Stop();" in step,
                "finishing waits for the workers, and a drained queue stops them")
    assert_true("bool		ReloadOrDefer( std::shared_ptr<imageDecodeJob_t> &job );" in image_h, "the route is part of idImage")


def main():
    test_picmip_cvars_are_declared_with_explicit_ranges()
    test_picmip_filter_buckets_are_shared_constants()
    test_picmip_only_reduces_the_diffuse_layer()
    test_picmip_filter_classifies_the_logical_image_name()
    test_one_policy_drives_every_loader_path()
    test_ceiling_is_applied_before_the_relative_reduction()
    test_generated_cache_key_tracks_the_active_reduction()
    test_exact_halvings_reuse_the_mip_chain_filter()
    test_reduction_changes_reload_images_without_a_restart()
    test_reload_decodes_on_workers_from_recorded_reads()
    test_picmip_is_documented_for_players()
    print("renderer_picmip_policy: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
