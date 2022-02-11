#version 460 core
#extension GL_GOOGLE_include_directive : enable
#include "pbr.glsl"

layout(binding = 0) uniform sampler2D gPos;
layout(binding = 1) uniform sampler2D gNormal;
layout(binding = 2) uniform sampler2D gDiffuse;
layout(binding = 3) uniform sampler2D gSpecular;
layout(binding = 4) uniform sampler2D shadowMap;

layout(push_constant) uniform CamPos{
	mat4 shadowMatrix;
	vec4 lightPos;
	vec3 camPos;
	int renderMode;
};

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 col;

void main(){
	float lightRadius = 100; //global (sphere) light radius

	vec4 pos = texture(gPos, inUV);
	vec4 normal = texture(gNormal, inUV);
	vec4 diffuseRoughness = texture(gDiffuse, inUV);
	vec4 specularMetallic = texture(gSpecular, inUV);

	switch(renderMode){
	case 1:
		col = vec4(pos.xyz, 1.f);
		return;
	case 2:
		col = vec4(normal.xyz, 1.f);
		return;
	case 3:
		col = vec4(diffuseRoughness.xyz, 1.f);
		return;
	case 4:
		col = vec4(specularMetallic.xyz, 1.f);
		return;
	}

	vec3 ambient = diffuseRoughness.xyz * 0.02;
	vec4 shadowCoord = shadowMatrix * pos;
	vec2 shadowIndex = shadowCoord.xy / shadowCoord.w;

	if(shadowCoord.w > 0 && //discard fragments behind the light
		shadowIndex.x >= 0 && shadowIndex.y >= 0 && //uv boundary [0 - 1] check
		shadowIndex.x <= 1 && shadowIndex.y <= 1){
		float lightDepth = texture(shadowMap, shadowIndex).r;
		float pixelDepth = shadowCoord.w;

		if(pixelDepth > lightDepth){//in shadow
			col = vec4(ambient, 1.f);
			return;
		}
	}

	vec3 L = lightPos.xyz - pos.xyz;
	float dist = length(L);
	L = normalize(L);
	vec3 V = normalize(camPos - pos.xyz);

	vec3 Lo = BRDF(L, V, normal.xyz, specularMetallic.w,
		diffuseRoughness.w, diffuseRoughness.xyz, specularMetallic.xyz,
		1, vec3(5.f, 5.f, 5.f), lightRadius);

	col = vec4(Lo + ambient, 1.f);
}
