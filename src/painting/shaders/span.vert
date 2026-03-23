#version 440

layout(location = 0) in vec2 dataCoord;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 extrudeDir;
layout(location = 3) in float extrudeWidth;
layout(location = 4) in vec2 isPixel;

layout(location = 0) out vec4 v_color;

layout(std140, binding = 0) uniform SpanParams {
    float width;
    float height;
    float yFlip;
    float dpr;
    float keyRangeLower;
    float keyRangeUpper;
    float keyAxisOffset;
    float keyAxisLength;
    float keyLogScale;
    float valRangeLower;
    float valRangeUpper;
    float valAxisOffset;
    float valAxisLength;
    float valLogScale;
};

float coordToPixel(float coord, float lower, float upper,
                   float offset, float length, float isLog) {
    float t;
    if (isLog > 0.5) {
        float safeCoord = max(coord, 1e-30);
        float safeLower = max(lower, 1e-30);
        float safeUpper = max(upper, 1e-30);
        t = (log(safeCoord) - log(safeLower)) / (log(safeUpper) - log(safeLower));
    } else {
        t = (coord - lower) / (upper - lower);
    }
    return t * length + offset;
}

void main() {
    float px = isPixel.x > 0.5
        ? dataCoord.x
        : coordToPixel(dataCoord.x, keyRangeLower, keyRangeUpper,
                        keyAxisOffset, keyAxisLength, keyLogScale);
    float py = isPixel.y > 0.5
        ? dataCoord.y
        : coordToPixel(dataCoord.y, valRangeLower, valRangeUpper,
                        valAxisOffset, valAxisLength, valLogScale);

    px += extrudeDir.x * extrudeWidth;
    py += extrudeDir.y * extrudeWidth;

    float ndcX = (px * dpr / width) * 2.0 - 1.0;
    float ndcY = yFlip * ((py * dpr / height) * 2.0 - 1.0);
    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
    v_color = color;
}
