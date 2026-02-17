// data/shaders/background.frag
#version 450

layout(binding = 0) uniform sampler2D backgroundImage;

layout(location = 0) out vec4 outColor;

vec2 getUV(vec2 fragCoord) {
    // Преобразуем координаты фрагмента в UV координаты
    // Фон занимает весь экран
    return fragCoord;
}

void main() {
    vec2 uv = gl_FragCoord.xy / textureSize(backgroundImage, 0);
    outColor = texture(backgroundImage, uv);
}
