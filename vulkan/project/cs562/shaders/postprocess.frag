#version 460 core

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 col;

layout(binding = 0) uniform sampler2D hdrImage;
layout(push_constant) uniform exposure{
	float e;
};

void main(){
	vec3 hdr = texture(hdrImage, inUV).xyz;
	hdr = e * hdr / (e * hdr + vec3(1, 1, 1));
	hdr = pow(hdr, vec3(1/2.2));
	col = vec4(hdr, 1.f);
}
