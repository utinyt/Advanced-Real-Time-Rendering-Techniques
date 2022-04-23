#version 460 core

layout(location = 0) in vec2 inUV;
layout(location = 0) out float aoFactor;

layout(binding = 0) uniform sampler2D gPos;
layout(binding = 1) uniform sampler2D gNormal;

layout(push_constant) uniform PS {
	vec4 camPos;
	float R;
	int N;
	float aoScale;
	float aoPower;
};

void main(){
	ivec2 xy = ivec2(gl_FragCoord.xy);
	float phi = (30 * xy.x ^ xy.y) + 10 * xy.x * xy.y;

	vec4 P = texture(gPos, inUV);
	float dist = distance(P.xyz, camPos.xyz);
	float PI = 3.141592;
	vec4 normal = texture(gNormal, inUV);
	float c = 0.1 * R;
	float sum = 0;

	for(int i = 0; i < N; ++i){
		float alpha = (i + 0.5f) / N;
		float h = alpha * R / dist;
		float theta = 2 * PI * alpha * (7 * N / 9) + phi;
		vec2 uv = inUV + h * vec2(cos(theta), sin(theta));
		vec4 Pi = texture(gPos, uv);
		vec3 wi = Pi.xyz - P.xyz;
		float integrand = max(0, dot(normal.xyz, wi) - 0.001 * dist) * (R - length(wi) >= 0 ? 1 : 0) / max(c*c, dot(wi, wi));
		sum += integrand;
	}
	sum *= 2 * PI * c / N;

	aoFactor = max(pow(1 - aoScale * sum, aoPower), 0);
}
