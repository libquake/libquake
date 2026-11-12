#version 330

// Bloom step 1: keep only what glows.
// The world shader stored the glow amount in alpha, so this is a multiply instead of a brightness threshold.
// A threshold would miss saturated colors like the red of a fullbright button.

in vec2 fragTexCoord;

uniform sampler2D texture0; // the scene

out vec4 finalColor;

void main()
{
    vec4 scene = texture(texture0, fragTexCoord);
    finalColor = vec4(scene.rgb * scene.a, 1.0);
}
