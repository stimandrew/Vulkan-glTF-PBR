// data/shaders/background.frag
#version 450

layout(binding = 0) uniform sampler2D backgroundImage;

layout(location = 0) out vec4 outColor;

// Uniform буфер для передачи параметров области рендеринга и пропорций
layout(push_constant) uniform PushConstants {
    vec2 viewportSize;      // Размер области рендеринга (ширина, высота)
    vec2 imageSize;         // Размер исходного изображения (ширина, высота)
    vec2 offset;            // Смещение для центрирования (если нужно)
} pushConstants;

void main() {
    // Получаем координаты фрагмента в пределах viewport'а
    vec2 fragCoord = gl_FragCoord.xy - pushConstants.offset;

    // Нормализуем координаты в пределах viewport'а (0..1)
    vec2 uv = fragCoord / pushConstants.viewportSize;

    // Вычисляем соотношения сторон
    float viewportAspect = pushConstants.viewportSize.x / pushConstants.viewportSize.y;
    float imageAspect = pushConstants.imageSize.x / pushConstants.imageSize.y;

    // Вычисляем масштаб так, чтобы изображение полностью помещалось
    float scale;
    vec2 finalUv;

    if (viewportAspect > imageAspect) {
        // Окно шире изображения - подгоняем по высоте
        // Черные полосы будут слева и справа
        scale = pushConstants.viewportSize.y / pushConstants.imageSize.y;
        float scaledWidth = pushConstants.imageSize.x * scale;
        float offsetX = (pushConstants.viewportSize.x - scaledWidth) * 0.5 / pushConstants.viewportSize.x;

        // Проверяем, не вышли ли мы за пределы видимой области
        if (uv.x < offsetX || uv.x > 1.0 - offsetX) {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        // Преобразуем координаты
        finalUv.x = (uv.x - offsetX) / (1.0 - 2.0 * offsetX);
        finalUv.y = uv.y;
    } else {
        // Окно уже изображения - подгоняем по ширине
        // Черные полосы будут сверху и снизу
        scale = pushConstants.viewportSize.x / pushConstants.imageSize.x;
        float scaledHeight = pushConstants.imageSize.y * scale;
        float offsetY = (pushConstants.viewportSize.y - scaledHeight) * 0.5 / pushConstants.viewportSize.y;

        // Проверяем, не вышли ли мы за пределы видимой области
        if (uv.y < offsetY || uv.y > 1.0 - offsetY) {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        // Преобразуем координаты
        finalUv.x = uv.x;
        finalUv.y = (uv.y - offsetY) / (1.0 - 2.0 * offsetY);
    }

    // Сэмплируем текстуру
    outColor = texture(backgroundImage, finalUv);
}
