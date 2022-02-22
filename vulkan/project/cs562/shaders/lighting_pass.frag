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
	float depthMin;
	float depthMax;
	float alpha;
};

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 col;

/**
* hamburger 4MSM
* @param s - filtered sample from moment shadow map
* @param zf - fragment depth
* @return G - shadow value [0 ~ 1]
*/
float getShadowValueG(vec4 s, float zf){
	vec4 s_ = (1 - alpha) *s + alpha * vec4(.5f);
	float zf2 = zf*zf;

	//float a = 1;
	float b = s_.x; //b1 / 1
	float c = s_.y; //b2 / 1
	float d = sqrt(s_.y - b*b);
	float e = (s_.z - b*c) / d;
	float f = sqrt(s_.w - c*c - e*e);
	
	//float c1_ = 1
	float c2_ = (zf - b) / d;
	float c3_ = (zf2 - c - e * c2_) / f;

	float c3 = c3_ / f;
	float c2 = (c2_ - e * c3) / d;
	float c1 = (1 - b * c2 - c * c3);

	//solve c3*z^2 + c2*z + c1 = 0
	float det = sqrt(c2*c2 - 4*c3*c1);
	float z2 = (-c2 - det) / (2 * c3);
	float z3 = (-c2 + det) / (2 * c3);
	if(z2 > z3){
		float t = z2;
		z2 = z3;
		z3 = t;
	}
	
	if(zf <= z2)
		return 0;
	else if(zf <= z3){
		float numerator = zf * z3 - s_.x * (zf + z3) + s_.y;
		float denominator = (z3 - z2) * (zf - z2);
		return numerator / denominator;
	}
	else{
		float numerator = z2 * z3 - s_.x * (z2+z3) + s_.y;
		float denominator = (zf - z2) * (zf - z3);
		return 1.f - (numerator / denominator);
	}
		
}

void main(){
	//debug view
	if(renderMode == 1){
		col = vec4(texture(shadowMap, inUV).xyz, 1.f); //shadow map
		return;
	}

	vec4 pos = texture(gPos, inUV);
	vec4 normal = texture(gNormal, inUV);
	vec4 diffuseRoughness = texture(gDiffuse, inUV);
	vec4 specularMetallic = texture(gSpecular, inUV);

	vec4 shadowCoord = shadowMatrix * pos;
	vec2 shadowIndex = shadowCoord.xy / shadowCoord.w;

	vec3 ambient = diffuseRoughness.xyz * 0.02;

	//BRDF
	vec3 L = lightPos.xyz - pos.xyz;
	float dist = length(L);
	L = normalize(L);
	vec3 V = normalize(camPos - pos.xyz);

	float lightRadius = 100; //global (sphere) light radius
	vec3 Lo = BRDF(L, V, normal.xyz, specularMetallic.w,
		diffuseRoughness.w, diffuseRoughness.xyz, specularMetallic.xyz,
		1, vec3(5.f, 5.f, 5.f), lightRadius);

	//Check shadow map range
	if(shadowCoord.w > 0 && //discard fragments behind the light
		shadowIndex.x >= 0 && shadowIndex.y >= 0 && //uv boundary [0 - 1] check
		shadowIndex.x <= 1 && shadowIndex.y <= 1){

		vec4 blurredShadowMap = texture(shadowMap, shadowIndex);
		float relativeFragmentDepth = (shadowCoord.w - depthMin) / (depthMax - depthMin);
		float G = getShadowValueG(blurredShadowMap, relativeFragmentDepth );
		col = vec4(Lo * (1.0 - G) + ambient, 1.f);
		return;
	}

	col = vec4(Lo + ambient, 1.f);
}
