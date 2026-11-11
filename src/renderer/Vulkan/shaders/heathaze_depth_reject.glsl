// Shared by the native heat-haze ports. _currentRender is copied after the
// first-person weapon and every other opaque surface has drawn, so a refracted
// read can land on something in front of the haze and paint a refracted copy
// of it into the world behind. When the scene depth at the refracted
// coordinate is nearer than this fragment, read the undistorted coordinate
// instead: the pixel straight behind the fragment, which passed the depth test.
// The renderer binds white (depth 1.0, never nearer) when the test is off.
//
// Both coordinates are 0..1 OpenGL window space; depthScale maps that space
// into the bottom-up _currentDepth capture.
vec2 HeatHazeRejectNearer(sampler2D sceneDepth, vec2 depthScale,
                          vec2 refracted, vec2 undistorted) {
    float depth = texture(sceneDepth, refracted * depthScale).r;
    return depth < gl_FragCoord.z ? undistorted : refracted;
}
