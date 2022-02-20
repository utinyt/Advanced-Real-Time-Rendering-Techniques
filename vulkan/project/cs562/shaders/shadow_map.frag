#version 460 core

layout(location = 0) in vec4 inPos;
layout(location = 0) out vec4 outDepth;

layout(push_constant) uniform PS {
	mat4 model;
	mat4 lightProjView;
	float depthMin;
	float depthMax;
};

void main(){
	float relativeDepth = (inPos.w - depthMin) / (depthMax - depthMin);

	float d2 = relativeDepth * relativeDepth;
	float d3 = d2 * relativeDepth;
	float d4 = d3 * relativeDepth;

	outDepth = vec4(relativeDepth, d2, d3, d4);
//	float d2 = inPos.w * inPos.w;
//	float d3 = d2 * inPos.w;
//	float d4 = d3 * inPos.w;
//
//	outDepth = vec4(inPos.w, d2, d3, d4);
}
