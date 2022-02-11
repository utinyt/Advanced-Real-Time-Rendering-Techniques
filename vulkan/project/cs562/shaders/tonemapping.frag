#version 460 core

layout(binding = 0) uniform sampler2D hdrImage;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 col;

void main(){
	vec3 hdr = texture(hdrImage, inUV).xyz;
	hdr  /= (hdr + vec3(1.0));
	hdr  = pow(hdr, vec3(1.0 / 2.2));
	col = vec4(hdr, 1.f);
}
