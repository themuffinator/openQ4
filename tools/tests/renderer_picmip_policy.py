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
IMAGE_MANAGER_CPP = Path("src") / "renderer" / "ImageManager.cpp"
IMAGE_LOAD_CPP = Path("src") / "renderer" / "Image_load.cpp"
IMAGE_PROCESS_CPP = Path("src") / "imagetools" / "Image_process.cpp"
IMAGE_FILES_CPP = Path("src") / "imagetools" / "Image_files.cpp"
RENDER_SYSTEM_CPP = Path("src") / "renderer" / "RenderSystem.cpp"


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
        "if ( usage == TD_DIFFUSE && R_ImagePicmipFilterAllows( name ) ) {" in image_manager,
        "picmip must be gated on TD_DIFFUSE so bump, specular, light, sky, font, and 2D images keep their authored size",
    )
    assert_true(
        "policy.mipShift = Max( 0, image_picmip.GetInteger() );" in image_manager,
        "the diffuse gate should be the only thing that sets a mip shift",
    )
    assert_true(
        "\tif ( !allowDownSize ) {\n\t\treturn;\n\t}" in image_manager,
        "'nopicmip' materials and presentation namespaces must opt out before any reduction is chosen",
    )


def test_picmip_filter_classifies_the_logical_image_name():
    image_manager = read_repo_file(IMAGE_MANAGER_CPP)

    assert_true("static bool R_ImagePicmipFilterAllows( const char *name )" in image_manager, "picmip should have a path filter")
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

    assert_true("struct imageDownsizePolicy_t {" in image_h, "the reduction contract should be one shared struct")
    for member in ("int			maxDimension;", "int			mipShift;", "int			minDimension;"):
        assert_true(member in image_h, f"missing policy member {member!r}")
    assert_true(
        "void R_GetImageDownsizePolicy( const char *name, textureUsage_t usage, bool allowDownSize, imageDownsizePolicy_t &policy );" in image_h,
        "the policy builder should be shared, not duplicated per loader",
    )

    assert_true(
        "bool R_LoadPrecompressedDDS(const char* name, idBinaryImage& image, ID_TIME_T* timestamp, textureUsage_t usage, const imageDownsizePolicy_t& downsizePolicy, bool useMipmaps);" in image_h,
        "the DDS loader must take the full policy, not just a size ceiling",
    )
    assert_true(
        "const int firstLevel = R_ImageDownsizePolicyMipSkip( downsizePolicy, selectedWidth, selectedHeight, (int)info.numLevels );" in image_files,
        "the DDS loader should select a mip level instead of decompressing and resampling",
    )

    for call in (
        "R_GetImageDownsizePolicy( GetName(), usage, allowDownSize, precompressedDownsizePolicy );",
        "R_DownsizeLoadedImageData( GetName(), usage, allowDownSize, pic, width, height );",
        "R_DownsizeLoadedCubeImageData( GetName(), usage, allowDownSize, pics, size );",
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
        "static void			GetGeneratedName(idStr& _name, const char* _policyName, const textureUsage_t& _usage, const cubeFiles_t& _cube, bool allowDownSize = true, unsigned int flags = 0);" in image_h,
        "the cache key must be told which logical name the policy was classified against",
    )
    assert_true(
        image_load.count("GetGeneratedName( generatedName, GetName(), usage, cubeFiles, allowDownSize, flags );") == 2,
        "both generated-name call sites must classify against the logical image name, including the DDS replacement site",
    )

    assert_true(
        "static unsigned int R_GetImageDownsizeSignature( const char *name, textureUsage_t usage, bool allowDownSize ) {" in image_load,
        "the cache signature should be derived from the shared policy",
    )
    # The trailing byte is a revision of the reduction arithmetic itself. Bump it
    # here and in Image_load.cpp together whenever the pixels a given policy
    # produces change, or players keep serving stale sizes out of their cache.
    assert_true(
        "unsigned int signature = ( static_cast<unsigned int>( policy.maxDimension ) << 8 ) ^ static_cast<unsigned int>( usage ) ^ 0x6F713401u;" in image_load,
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
        "byte *next = gammaMips ? R_MipMapWithGamma( source, level, levelHeight ) : R_MipMap( source, level, levelHeight );" in image_load,
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
        image_load.count("return R_ResampleTexture( pic, width, height, scaledWidth, scaledHeight );") >= 3,
        "non power-of-two and failed reductions must fall back to the general resampler with the original source size",
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
    assert_true(
        "images[i]->Reload( true );" in image_manager,
        "a reduction change should reload the file-backed images",
    )
    assert_true(
        "if ( images[i]->GetOpts().isPersistant ) {" in image_manager,
        "the reduction reload must skip render targets and glyph atlases, which Reload() would blank",
    )
    assert_true(
        "if ( insideLevelLoad ) {" in image_manager,
        "a level load already touches every image, so the reload must not double up on it",
    )

    # Every cvar is born CVAR_MODIFIED, so without a startup prime the first
    # rendered frame of every launch would reload every image for no reason.
    assert_true("void idImageManager::PrimeCvars() {" in image_manager, "startup should swallow the born-modified state")
    assert_true("\tPrimeCvars();" in image_manager, "idImageManager::Init should prime the reduction cvars")
    assert_true("void				PrimeCvars();" in image_h, "PrimeCvars should be part of the image manager interface")


def test_picmip_is_documented_for_players():
    display_settings = read_repo_file(Path("docs/user") / "display-settings.md")

    assert_true("## Texture Quality (Picmip and Downsizing)" in display_settings, "picmip should have a user-facing section")
    for snippet in (
        "`image_picmip`",
        "`image_picmipFilter`",
        "`image_picmipMinSize`",
        "diffuse layer of materials",
        "A material that declares `nopicmip` opts out of both.",
    ):
        assert_true(snippet in display_settings, f"missing picmip documentation detail {snippet!r}")


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
    test_picmip_is_documented_for_players()
    print("renderer_picmip_policy: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
