#version 450
// Triangle plein écran ; la direction de vue est reconstruite dans le fragment.
layout(location = 0) out vec2 vNdc;

void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
