#version 330

// Bloom step 2: gaussian blur along one axis. It runs twice, horizontally and then vertically,
// which gives the same result as a 2D blur for a fraction of the texture reads.
//
// The 9 taps of the gaussian are read with 5 fetches: between two texels the bilinear filter
// returns their weighted average, so one fetch at the right offset counts as two taps.

in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform vec2 direction; // one texel along the blur axis, (1 / width, 0) or (0, 1 / height)

out vec4 finalColor;

const float offsets[3] = float[](0.0, 1.3846153846, 3.2307692308);
const float weights[3] = float[](0.2270270270, 0.3162162162, 0.0702702703);

void main()
{
    vec3 color = texture(texture0, fragTexCoord).rgb * weights[0];
    for (int i = 1; i < 3; i++) {
        vec2 offset = direction * offsets[i];
        color += texture(texture0, fragTexCoord + offset).rgb * weights[i];
        color += texture(texture0, fragTexCoord - offset).rgb * weights[i];
    }
    finalColor = vec4(color, 1.0);
}
