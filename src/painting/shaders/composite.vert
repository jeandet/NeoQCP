#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;

layout(std140, binding = 1) uniform LayerParams {
    float translateX;
    float translateY;
    float viewportW;
    float viewportH;
    float yFlip;
} lp;

void main()
{
    float dx = (lp.translateX / lp.viewportW) * 2.0;
    float dy = lp.yFlip * (lp.translateY / lp.viewportH) * 2.0;
    v_texcoord = texcoord;
    gl_Position = vec4(position.x + dx, position.y + dy, 0.0, 1.0);
}
