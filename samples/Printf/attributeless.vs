#version 330
 
const vec2 data[4] = vec2[](
  vec2(-1.0,  1.0),
  vec2(-1.0, -1.0),
  vec2( 1.0,  1.0),
  vec2( 1.0, -1.0)
);

out vec2 vTexCoords;
 
void main() {
  vTexCoords = (data[gl_VertexID] + 1) / 2;
  gl_Position = vec4(data[gl_VertexID], 0.0, 1.0);
  
  printf("Vertex %i is at %.1f", gl_VertexID, gl_Position.xyz);
}
