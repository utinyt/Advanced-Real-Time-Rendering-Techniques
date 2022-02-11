#version 460 core

layout(location = 0) in vec4 inPos;
layout(location = 0) out float outDepth;

void main(){
	outDepth = inPos.w;
}
