#version 450

layout(location = 0) in vec2 inPos;

layout(push_constant) uniform PushConstants {
    vec2 screenSize;      // ширина и высота экрана
    vec2 rectLeftTop;     // левый верхний угол рамки
    vec2 rectRightBottom; // правый нижний угол рамки
} push;

void main() {
    // Вычисляем абсолютные экранные координаты для текущей вершины
    float screenX = mix(push.rectLeftTop.x, push.rectRightBottom.x, inPos.x);
    float screenY = mix(push.rectLeftTop.y, push.rectRightBottom.y, inPos.y);

    // Преобразуем экранные координаты в NDC
    float ndcX = (screenX / push.screenSize.x) * 2.0 - 1.0;
    float ndcY = 1.0 - (screenY / push.screenSize.y) * 2.0; // Инвертируем Y

    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
}
