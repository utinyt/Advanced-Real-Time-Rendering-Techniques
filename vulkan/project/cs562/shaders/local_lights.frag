#version 460 core
#extension GL_GOOGLE_include_directive : enable
#include "pbr.glsl"

layout(push_constant) uniform LightInfo {
	mat4 model;
	vec3 color;
	float radius;
	vec4 lightCenter;
	vec3 camPos;
	int renderMode;
	bool disableLocalLight;
};

layout(set = 1, binding = 0) uniform sampler2D gPos;
layout(set = 1, binding = 1) uniform sampler2D gNormal;
layout(set = 1, binding = 2) uniform sampler2D gDiffuse;
layout(set = 1, binding = 3) uniform sampler2D gSpecular;

layout(location = 0) in vec4 inPos;
layout(location = 0) out vec4 col;

void main(){
	if(renderMode != 0 || disableLocalLight == true){
		col = vec4(0, 0, 0, 1);
		return;
	}
	const vec2 uv = gl_FragCoord.xy/textureSize(gPos, 0);
	const vec4 pos = texture(gPos, uv);
	const vec4 normal = texture(gNormal, uv);
	const vec4 diffuseRoughness = texture(gDiffuse, uv);
	const vec4 specularMetallic = texture(gSpecular, uv);

	vec3 L = lightCenter.xyz - pos.xyz;
	const float dist = length(L);

	if(dist > radius){
		col = vec4(0, 0, 0, 1);
		return;
	}

	L = normalize(L);
	const vec3 V = normalize(camPos - pos.xyz);

	vec3 Lo = BRDF(L, V, normal.xyz, specularMetallic.w,
		diffuseRoughness.w, diffuseRoughness.xyz, specularMetallic.xyz,
		dist, color, radius);

	Lo = Lo / (Lo + vec3(1.f));
	col = vec4(Lo, 1.f);
}
