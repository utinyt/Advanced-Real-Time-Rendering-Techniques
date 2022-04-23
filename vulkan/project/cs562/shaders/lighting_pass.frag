#version 460 core

layout(binding = 0) uniform sampler2D gPos;
layout(binding = 1) uniform sampler2D gNormal;
layout(binding = 2) uniform sampler2D gDiffuse;
layout(binding = 3) uniform sampler2D gSpecular;
layout(binding = 4) uniform sampler2D shadowMap;
layout(binding = 5) uniform sampler2D irradianceMap;
layout(binding = 6) uniform sampler2D skydomeTexture;
layout(binding = 7) uniform Hammersely{
	vec4 hammersely[20];
	uint N;
};

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

const float PI = 3.14159265359;

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

/*
* return corresponding uv coordinate of environmental sphere map
*/
vec2 sphereMap(vec3 normal){
	vec2 uv;
	uv.x = 0.5 - (atan(normal.x, normal.z) / (2 * PI));
	uv.y = acos(normal.y) / PI;
	return uv;
}

/*
* return corresponding direction vector of environmental sphere map
*/
vec3 inverseSphereMap(vec2 uv){
	vec3 normal;
	float s = sin(PI * uv.y);
	normal.x = cos(2 * PI * (0.5 - uv.x)) * s;
	normal.y = sin(2 * PI * (0.5 - uv.x)) * s;
	normal.z = cos(PI * uv.y);
	return normal;
}

/*
* d term
*/
float GGXdistribution(vec3 N, vec3 H, float roughness){
	float ggxRoughnessSquared = 2 / (roughness + 2);
	float d = dot(N, H) * dot(N, H) * (ggxRoughnessSquared - 1) + 1;
	float denominator = PI * d * d;
	return ggxRoughnessSquared / denominator;
}

/*
* f term
*/
vec3 fresnelSchlick(float hDotV, vec3 F0){;
	return F0 + (1.0 - F0) * pow(clamp(1.0 - hDotV, 0.0, 1.0), 5.0);
}

/*
* g term
*/
float G1(vec3 v, vec3 m, vec3 normal, float roughness){
	float vNSquared = dot(v, normal);
	vNSquared *= vNSquared;
	float tanThetaSquared = (1 - vNSquared) / vNSquared;
	float ggxRoughnessSquared = 2 / (roughness + 2);
	return 2 / (1 + sqrt(1 + ggxRoughnessSquared * tanThetaSquared));
}

float G(vec3 L, vec3 V, vec3 H, vec3 normal, float roughness){
	return G1(L, H, normal, roughness) * G1(V, H, normal, roughness);
}

void main(){
	//debug view
	if(renderMode == 1){
		col = vec4(texture(shadowMap, inUV).xyz, 1.f); //shadow map
		return;
	}

	//skydome
	vec4 normal = texture(gNormal, inUV);
	if(normal.w == -1){
		col = vec4(textureLod(skydomeTexture, sphereMap(normalize(normal.xyz)), 0).xyz, 1.f);
		//col = vec4(1, 1, 1, 1);
		return;
	}

	vec4 pos = texture(gPos, inUV);
	vec4 diffuseRoughness = texture(gDiffuse, inUV);
	float ggxRoughness = sqrt(2 / (diffuseRoughness.w + 2));
	vec4 specularMetallic = texture(gSpecular, inUV);

	//BRDF
	const vec3 V = normalize(camPos - pos.xyz);
	const vec3 R = normalize(2 * dot(normal.xyz, V) * normal.xyz - V);
	const vec3 A = normalize(vec3(-R.y, R.x, 0));
	const vec3 B = normalize(cross(R, A));

	//diffuse term
	vec3 kd = vec3(1.f) - specularMetallic.xyz;
	kd *= 1 - specularMetallic.w;
	const vec3 diffuse = kd * diffuseRoughness.xyz / PI * textureLod(irradianceMap, sphereMap(normalize(normal.xyz)), 0).xyz;

	//specular
	vec3 spec = vec3(0);
	vec2 texSize = textureSize(skydomeTexture, 0);

	for(int i = 0; i < N; ++i){
		float r1 = hammersely[i * 2 / 4][(i * 2) % 4];
		float r2 = hammersely[i * 2 / 4][(i * 2) % 4 + 1];
		float theta = atan(ggxRoughness * sqrt(r2) / sqrt(1 - r2));
		vec2 uv = vec2(r1, theta / PI);
		vec3 dir = normalize(inverseSphereMap(uv));
		vec3 wk = normalize(dir.x * A + dir.y * B + dir.z * R);
		vec3 H = normalize(wk + V);
		float levelTerm = 0.5 * log2(texSize.x * texSize.y / float(N));
		float level = levelTerm - 0.5 * log2(GGXdistribution(normal.xyz, H, diffuseRoughness.w));
		vec3 Liwk = textureLod(skydomeTexture, sphereMap(wk), max(level, 0)).xyz;
		
		spec += G(wk, V, H, normal.xyz, diffuseRoughness.w) * fresnelSchlick(dot(wk, H), specularMetallic.xyz) / 
			(4 * dot(wk, normal.xyz) * dot(V, normal.xyz)) *
			Liwk * dot(wk, normal.xyz);
	}
	spec /= N;

	//	vec4 shadowCoord = shadowMatrix * pos;
	//	vec2 shadowIndex = shadowCoord.xy / shadowCoord.w;
	//Check shadow map range
//	if(shadowCoord.w > 0 && //discard fragments behind the light
//		shadowIndex.x >= 0 && shadowIndex.y >= 0 && //uv boundary [0 - 1] check
//		shadowIndex.x <= 1 && shadowIndex.y <= 1){
//
//		vec4 blurredShadowMap = texture(shadowMap, shadowIndex);
//		float relativeFragmentDepth = (shadowCoord.w - depthMin) / (depthMax - depthMin);
//		float G = getShadowValueG(blurredShadowMap, relativeFragmentDepth );
//		col = vec4(Lo * (1.0 - G), 1.f);
//		return;
//	}

	col = vec4(diffuse + spec, 1.f);
	//col = vec4(texture(irradianceMap, inUV).xyz, 1.f);
}
