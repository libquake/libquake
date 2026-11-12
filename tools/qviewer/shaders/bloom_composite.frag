#version 330

// Bloom step 3: add the blurred glow to the scene.
// Two blur sizes: the small one gives a tight halo, the large one a soft wide glow.

in vec2 fragTexCoord;

uniform sampler2D texture0;    // the scene
uniform sampler2D bloomSmall;  // blurred at half resolution
uniform sampler2D bloomLarge;  // blurred at a quarter resolution
uniform float bloomStrength;

out vec4 finalColor;

void main()
{
    vec3 scene = texture(texture0, fragTexCoord).rgb;
    vec3 glow = texture(bloomSmall, fragTexCoord).rgb + texture(bloomLarge, fragTexCoord).rgb;
    finalColor = vec4(scene + glow * bloomStrength, 1.0);
}
