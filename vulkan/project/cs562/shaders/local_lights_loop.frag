#version 460 core
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "pbr.glsl"

#define LOCAL_LIGHTS_COUNT_SQRT 26

struct Light{
	mat4 model;
	vec3 color;
	float radius;
	vec4 lightCenter;
};

layout(set = 0, binding = 0) uniform LightInfo{
	Light lights[LOCAL_LIGHTS_COUNT_SQRT * LOCAL_LIGHTS_COUNT_SQRT];
};

layout(set = 1, binding = 0) uniform sampler2D gPos;
layout(set = 1, binding = 1) uniform sampler2D gNormal;
layout(set = 1, binding = 2) uniform sampler2D gDiffuse;
layout(set = 1, binding = 3) uniform sampler2D gSpecular;

layout(push_constant) uniform PushConstant{
	vec3 camPos;
	int renderMode;
	int nbLight;
};

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 col;

void main(){
	if(renderMode != 0){
		col = vec4(0, 0, 0, 1);
		return;
	}
	const vec4 pos = texture(gPos, inUV);
	const vec4 normal = texture(gNormal, inUV);
	const vec4 diffuseRoughness = texture(gDiffuse, inUV);
	const vec4 specularMetallic = texture(gSpecular, inUV);

	vec3 LoSum = vec3(0.0);
	for(int i = 0; i < nbLight; ++i){
		Light light = lights[i]; 
		vec3 L = light.lightCenter.xyz - pos.xyz;
		const float dist = length(L);

		if(dist > light.radius){
			continue;
		}

		L = normalize(L);
		const vec3 V = normalize(camPos - pos.xyz);

		vec3 Lo = BRDF(L, V, normal.xyz, specularMetallic.w,
			diffuseRoughness.w, diffuseRoughness.xyz, specularMetallic.xyz,
			dist, light.color, light.radius);
		Lo = Lo / (Lo + vec3(1.f));
		LoSum += Lo;
	}
	col = vec4(LoSum, 1.f);
}
