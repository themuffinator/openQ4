#!/usr/bin/env python3
"""Pin fail-after-primary compatibility fallbacks for incomplete retail media."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function {signature!r}")
    brace = source.find("{", start)
    if brace < 0:
        raise AssertionError(f"missing body for {signature!r}")
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise AssertionError(f"unterminated body for {signature!r}")


def require_in_order(source: str, snippets: tuple[str, ...], message: str) -> None:
    cursor = 0
    for snippet in snippets:
        position = source.find(snippet, cursor)
        if position < 0:
            raise AssertionError(f"{message}: missing or out of order {snippet!r}")
        cursor = position + len(snippet)


def test_images_try_authored_media_before_shape_compatible_stock_fallbacks() -> None:
    source = read("src/renderer/Image_load.cpp")
    image_header = read("src/renderer/Image.h")
    image_program = read("src/imagetools/Image_program.cpp")
    if "bool R_LoadImageProgram(" not in image_header or "bool R_LoadImageProgram(" not in image_program:
        raise AssertionError("image-program source selection must expose its load-success result")
    if "return loaded;" not in image_program:
        raise AssertionError("image-program source selection must return the parser load result")

    resolver = function_body(source, "static const char *R_FindMissingQ4StockImageFallback(")
    expected = {
        "gfx/effects/fluids_drips/brown_bubble":
            "gfx/effects/fluids_drips/bubble_alpha.tga",
        "gfx/effects/fluids_drips/brown_bubble_half":
            "gfx/effects/fluids_drips/bubble_half.tga",
        "gfx/effects/fluids_drips/brown_splash_line":
            "gfx/effects/fluids_drips/splash_line.tga",
    }
    for missing, replacement in expected.items():
        if missing not in resolver or replacement not in resolver:
            raise AssertionError(f"missing retail image fallback {missing!r} -> {replacement!r}")

    for missing, replacement in (
        ('a4x_tactical/q4x_tt_body_s', 'q4x_tactical/q4x_tt_body_s'),
        ('gfx/effects/fire/p_fire2a', 'gfx/effects/fire/fire2a.tga'),
    ):
        if missing not in resolver or replacement not in resolver or '"q4xbase"' not in resolver:
            raise AssertionError('Awakening image fallback missing or unscoped')
    downsize = image_program.split('if (!token.Icmp("downsize")) {', 1)[1].split('// jmarshall end', 1)[0]
    if 'const bool loaded = R_ParseImageProgram_r' not in downsize or 'return loaded;' not in downsize:
        raise AssertionError('downsize hides a missing source from fallback selection')

    selector = function_body(source, "static bool R_SelectMissingQ4StockImageSource(")
    require_in_order(
        selector,
        (
            "R_FindMissingQ4StockImageFallback( imageName )",
            "if ( stockFallbackName == NULL ) {",
            "selectedSourceName = imageName;",
            "selectedSourceTime = 0;",
            "if ( R_LoadImageProgram( imageName, NULL, NULL, NULL, &selectedSourceTime ) ) {",
            "selectedSourceTime = 0;",
            "if ( R_LoadImageProgram( stockFallbackName, NULL, NULL, NULL, &selectedSourceTime ) ) {",
            "selectedSourceName = stockFallbackName;",
            "stockFallbackSelected = true;",
        ),
        "exact stock candidates must prefer a successfully loaded authored source before fallback",
    )
    if "selectedSourceTime > 0" in selector or "selectedSourceTime <= 0" in selector:
        raise AssertionError("stock source selection must not infer load success from PK4 timestamps")

    cache_identity = function_body(
        source, "static void R_AddMissingQ4StockImageCacheIdentity("
    )
    require_in_order(
        cache_identity,
        (
            "generatedName.ExtractFileExtension( extension );",
            "generatedName.StripFileExtension();",
            'stockFallbackSelected ? "#q4stockfallback" : "#q4authored"',
            "generatedName.SetFileExtension( extension );",
        ),
        "authored and fallback sources need distinct non-legacy generated cache paths",
    )

    load = function_body(source, "void idImage::ActuallyLoadImage(")
    require_in_order(
        load,
        (
            "R_LoadImageProgramForDeclaredUsage( fallbackLoadSourceName, &pic",
            "if ( pic == NULL && !q4StockImageCandidateResolved ) {",
            "R_FindMissingQ4StockImageFallback( GetName() )",
            "R_LoadImageProgramForDeclaredUsage( stockFallbackName, &pic",
            "selectedSourceName = stockFallbackName;",
            'idLib::Warning( "Couldn\'t load image:',
        ),
        "image compatibility must preserve overrides and only default after the fallback fails",
    )
    require_in_order(
        load,
        (
            "R_ResolvePreferredDDSImageSource( GetName(), preferredDDSName",
            "if ( preferredDDSImage && !fileSystem->InProductionMode() ) {",
            "bool q4StockImageCandidateResolved = false;",
            "if ( !explicitDDSImage && !preferredDDSImage && cubeFiles == CF_2D ) {",
            "q4StockImageCandidateResolved = R_SelectMissingQ4StockImageSource( GetName(), selectedSourceName,",
            "sourceFileTimeKnown = true;",
            "R_AddMissingQ4StockImageCacheIdentity( generatedName, stockFallbackSelected );",
            "idStr selectedLoadSourceName = preferredDDSImage ? preferredDDSName : selectedSourceName;",
            "idBinaryImage im( generatedName );",
            "binaryFileTime = im.LoadFromGeneratedFileUnchecked();",
        ),
        "exact fallback source and cache identity must be selected before an existing generated cache is opened",
    )
    require_in_order(
        load,
        (
            "q4StockImageCandidateResolved = R_SelectMissingQ4StockImageSource( GetName(), selectedSourceName,",
            "idStr selectedLoadSourceName = preferredDDSImage ? preferredDDSName : selectedSourceName;",
            "R_LoadImageProgramForDeclaredUsage( fallbackLoadSourceName, &pic",
            "if ( pic == NULL && !q4StockImageCandidateResolved ) {",
        ),
        "decode must remain locked to the source identity selected for the generated cache",
    )
    cache_open = load.find("idBinaryImage im( generatedName );")
    cache_validation = load.find("auto acceptGeneratedImage =")
    if cache_open < 0 or cache_validation < cache_open:
        raise AssertionError("generated cache validation ordering is malformed")
    if "R_FindMissingQ4StockImageFallback(" in load[cache_open:cache_validation]:
        raise AssertionError("stock fallback selection must not be deferred until after cache open")

    reload_body = function_body(source, "void idImage::Reload(")
    require_in_order(
        reload_body,
        (
            "idStr currentSourceName = imgName;",
            "R_ResolvePreferredDDSImageSource( imgName, preferredDDSName",
            "R_SelectMissingQ4StockImageSource( imgName, currentSourceName,",
            "R_LoadImageProgram( imgName, NULL, NULL, NULL, &current );",
            "loadedSourceName.Icmp( currentSourceName )",
            "if ( !sourceSelectionChanged && R_ImageSourceStampCurrent( current, sourceFileTime ) ) {",
        ),
        "reload must keep a fallback source stable while allowing a new authored source to take priority",
    )
    if "R_LoadImageProgramForDeclaredUsage(" in reload_body:
        raise AssertionError("timestamp-only reload checks must not mutate the image's declared usage")


def test_sound_tries_authored_sample_before_the_retail_family_fallback() -> None:
    source = read("src/sound/OpenAL/AL_SoundSample.cpp")
    resolver = function_body(source, "static void SoundSample_AppendMissingQ4StockFallback(")
    for snippet in (
        '"sound/ambience/water/splash_big"',
        '"sound/ambience/water/splash_small02"',
        "canonicalName.BackSlashesToSlashes();",
        "canonicalName.StripFileExtension();",
    ):
        if snippet not in resolver:
            raise AssertionError(f"missing sound fallback contract {snippet!r}")

    # LoadResource and LoadResourceAsync share the probe order
    variants = function_body(source, "static void SoundSample_BuildVariants(")
    require_in_order(
        variants,
        (
            "SoundSample_AppendUniqueSampleVariant( sampleVariants, baseSampleName );",
            "SoundSample_AppendMissingQ4StockFallback( sampleVariants, baseSampleName );",
        ),
        "sound compatibility must probe the authored sample before its fallback",
    )
    load = function_body(source, "void idSoundSample_OpenAL::LoadResource(")
    require_in_order(
        load,
        (
            "SoundSample_BuildVariants( GetName(), sampleVariants );",
            "for( int i = 0; i < sampleVariants.Num(); i++ )",
            'idLib::Warning( "Couldn\'t load sound',
        ),
        "sound compatibility must probe the authored sample before its fallback",
    )


def main() -> int:
    test_images_try_authored_media_before_shape_compatible_stock_fallbacks()
    test_sound_tries_authored_sample_before_the_retail_family_fallback()
    print("stock media fallback checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
