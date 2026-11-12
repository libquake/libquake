#version 330

// Vertex shader for every brush surface (walls and sky).
// raylib fills the attributes from the Mesh and the matrices from the camera and the model.

in vec3 vertexPosition;
in vec2 vertexTexCoord;  // texture uv
in vec2 vertexTexCoord2; // lightmap uv

uniform mat4 mvp;      // model * view * projection
uniform mat4 matModel; // model to world, brush entities can be moved

out vec2 texUV;
out vec2 lightmapUV;
out vec3 worldPos;

void main()
{
    texUV = vertexTexCoord;
    lightmapUV = vertexTexCoord2;
    worldPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
