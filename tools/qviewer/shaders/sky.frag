#version 330

// Quake's sky: two layers on a flattened sphere around the viewer.
// The back layer is solid and scrolls at 8 units per second, the front layer (clouds) at 16.

in vec2 texUV;
in vec2 lightmapUV;
in vec3 worldPos;

uniform sampler2D texture0; // back layer
uniform sampler2D texture1; // front layer, alpha 0 where the back layer shows through

uniform vec3 viewPos;
uniform float time;
uniform vec4 fog;      // same fog as the world
uniform float skyFog;  // how much fog covers the sky, the "skyfog" key (default 0.5)
uniform float wireframe;

out vec4 finalColor;

void main()
{
    if (wireframe > 0.5) {
        finalColor = vec4(0.2, 0.6, 1.0, 1.0);
        return;
    }

    // direction from the eye in quake axes (z up), squashed vertically like the original
    vec3 d = worldPos - viewPos;
    vec3 dir = vec3(d.x, -d.z, d.y * 3.0);
    vec2 st = dir.xy * (6.0 * 63.0 / length(dir));

    vec2 backUV = (st + mod(time * 8.0, 128.0)) / 128.0;
    vec2 frontUV = (st + mod(time * 16.0, 128.0)) / 128.0;
    vec4 back = texture(texture0, backUV);
    vec4 front = texture(texture1, frontUV);
    vec3 color = mix(back.rgb, front.rgb, front.a);

    // the sky does not get closer, so the fog is a fixed blend
    if (fog.a > 0.0)
        color = mix(color, fog.rgb, skyFog);

    finalColor = vec4(color, 0.0);
}
