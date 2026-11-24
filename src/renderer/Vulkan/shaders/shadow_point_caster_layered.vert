#version 450
#extension GL_ARB_shader_viewport_layer_array : require

// openQ4 Vulkan point-light shadow-map caster, layered vertex stage
// (r_shadowMapPointLayered).
//
// shadow_point_caster.vert with one addition: the cube is bound as a single
// six-layer depth attachment, and each draw goes to the face its push block
// names. Everything else is that shader's, so a face holds exactly the depth
// it holds when it is drawn in a rendering scope of its own. Keep the two in
// step.
//
// Writing gl_Layer from a vertex stage needs VK_EXT_shader_viewport_index_layer;
// the module is only created where the device has it.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(push_constant) uniform CasterPushConstants {
    mat4 mvp;        // model -> cube-face view space
    vec4 depthRow;   // x: zA, y: zB, z: far envelope, w: cube face
    vec4 alphaS;     // alpha-test texture matrix S row; z = slope-scale depth factor
    vec4 alphaT;     // alpha-test texture matrix T row; z = constant depth offset
    vec4 params;     // x: alpha mode, y: alphaRef, z: alphaScale, w: alpha-hash mode/seed
} pc;

layout(location = 0) out vec2 vAlphaTexCoord;
layout(location = 1) out vec3 vPointShadowVector;
layout(location = 2) out vec3 vAlphaHashCoord;

void main() {
    vec4 position = vec4(inPosition, 1.0);
    vec4 texCoord = vec4(inTexCoord, 0.0, 1.0);
    // texCoord.z is 0, so the depth-offset scalars packed into the matrix
    // rows' z components never contribute to the texture coordinate
    vAlphaTexCoord = vec2(dot(texCoord, pc.alphaS), dot(texCoord, pc.alphaT));
    vec4 viewPos = pc.mvp * position;
    // Recover world-axis order before interpolating the radial vector. The
    // face view is only a signed permutation, but length() otherwise sums
    // squared components in a different order on different cube faces.
    int face = int(pc.depthRow.w);
    if (face == 0) vPointShadowVector = vec3(-viewPos.z, -viewPos.y, -viewPos.x);
    else if (face == 1) vPointShadowVector = vec3(viewPos.z, -viewPos.y, viewPos.x);
    else if (face == 2) vPointShadowVector = vec3(viewPos.x, -viewPos.z, viewPos.y);
    else if (face == 3) vPointShadowVector = vec3(viewPos.x, viewPos.z, -viewPos.y);
    else if (face == 4) vPointShadowVector = vec3(viewPos.x, -viewPos.y, -viewPos.z);
    else vPointShadowVector = vec3(-viewPos.x, -viewPos.y, viewPos.z);
    // Shared across all six faces, unlike face-view coordinates.
    vAlphaHashCoord = inPosition;
    gl_Position = vec4(viewPos.xy, pc.depthRow.x * viewPos.z + pc.depthRow.y * viewPos.w, -viewPos.z);
    gl_Layer = face;
}
