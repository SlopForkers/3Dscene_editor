#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUv;
layout(location = 4) in vec4 aJoints;
layout(location = 5) in vec4 aWeights;

uniform mat4 uViewProj;
uniform mat4 uInstance;   // prop world transform (placement)
uniform mat4 uModel;      // node global transform within the model
uniform mat4 uJointMatrices[256];
uniform int  uHasSkin;

out vec3 vNormal;
out vec2 vUv;
out vec3 vWorldPos;
out mat3 vTBN;
out float vHasTangent;

void main() {
    vec4 pos = vec4(aPos, 1.0);
    vec4 nrm = vec4(aNormal, 0.0);
    if (uHasSkin == 1) {
        mat4 skinMat =
            aWeights.x * uJointMatrices[int(aJoints.x)] +
            aWeights.y * uJointMatrices[int(aJoints.y)] +
            aWeights.z * uJointMatrices[int(aJoints.z)] +
            aWeights.w * uJointMatrices[int(aJoints.w)];
        pos = skinMat * pos;
        nrm = skinMat * nrm;
    }

    vec4 world = uInstance * uModel * pos;
    vWorldPos = world.xyz;

    mat3 worldMat = mat3(uInstance * uModel);
    mat3 normalMat = transpose(inverse(worldMat));
    vNormal = normalize(normalMat * nrm.xyz);
    vUv = aUv;

    // Build a TBN basis if a tangent was provided (glTF stores w as handedness).
    if (length(aTangent.xyz) > 0.0) {
        vec3 T = normalize(normalMat * aTangent.xyz);
        vec3 N = vNormal;
        vec3 B = (aTangent.w < 0.0) ? -cross(N, T) : cross(N, T);
        vTBN = mat3(T, B, N);
        vHasTangent = 1.0;
    } else {
        vTBN = mat3(1.0);
        vHasTangent = 0.0;
    }

    gl_Position = uViewProj * world;
}
