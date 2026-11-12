#version 330

// Brush surfaces the way Quake draws them, plus distance fog.
//
// The output alpha is not transparency: it tells the bloom pass how much this pixel glows.

in vec2 texUV;
in vec2 lightmapUV;
in vec3 worldPos;

uniform sampler2D texture0; // albedo
uniform sampler2D texture1; // lightmap, 128 is normal brightness
uniform sampler2D texture2; // emission: the fullbright pixels, black everywhere else
uniform vec4 colDiffuse;

uniform vec3 viewPos;     // camera position, everything is in quake units
uniform vec4 fog;         // rgb: fog color, a: density like the "fog" key of FitzQuake (0 = no fog)
uniform float fullbright; // 1 ignores the lightmap
uniform float wireframe;  // 1 draws a flat color for the line overlay

out vec4 finalColor;

float maxComponent(vec3 v)
{
    return max(v.r, max(v.g, v.b));
}

// FitzQuake fog: exp2 falloff over the distance in quake units, the density is divided by 64
float fogAmount(vec3 position)
{
    float distance = length(position - viewPos);
    float x = fog.a / 64.0 * distance;
    return 1.0 - exp(-x * x);
}

void main()
{
    if (wireframe > 0.5) {
        finalColor = vec4(0.2, 1.0, 0.3, 1.0);
        return;
    }

    vec4 albedo = texture(texture0, texUV);
    // '{' textures: palette index 255 is a hole
    if (albedo.a < 0.5)
        discard;

    // 1. light: the lightmap stores half the light, so it can brighten up to twice the texture color
    vec3 light = texture(texture1, lightmapUV).rgb * 2.0;
    light = mix(light, vec3(1.0), fullbright);
    vec3 lit = albedo.rgb * colDiffuse.rgb * light;

    // 2. fullbright pixels keep their color no matter how dark it is around them.
    //    glow is the share of fullbright pixels in this (filtered) texel, 1 on a fullbright pixel.
    vec3 emission = texture(texture2, texUV).rgb;
    float glow = clamp(maxComponent(emission) / max(maxComponent(albedo.rgb), 0.001), 0.0, 1.0);
    vec3 color = mix(lit, emission, glow);

    // 3. what blooms: fullbrights fully, lit surfaces a little where the light saturates them
    float overbright = clamp(maxComponent(lit) - 1.0, 0.0, 1.0);
    float bloom = max(glow, overbright * 0.5);

    // 4. fog covers the color and the glow
    float f = fogAmount(worldPos);
    color = mix(color, fog.rgb, f);
    bloom *= 1.0 - f;

    finalColor = vec4(color, bloom);
}
