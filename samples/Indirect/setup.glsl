#version 430

layout (local_size_x = 1) in;

layout(std430, binding=0) buffer uIndirectBuffer {
  uint num_groups_x;
  uint num_groups_y;
  uint num_groups_z;
  
  uint count;
  uint instanceCount;
  uint first;
  uint baseInstance;
};

uniform int uCountX;
uniform int uCountY;

void main() {
  num_groups_x = (uCountX + 15) / 16;
  num_groups_y = (uCountY + 15) / 16;
  num_groups_z = 1;
  
  count = (uCountX * uCountY) * 6;
  instanceCount = 1;
  first = 0;
  baseInstance = 0;
}
