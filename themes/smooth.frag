// Smooths the thickness of the inflated shapes (shape.frag): scenery and
// sprites, by a 5x5 Gaussian where outside the shape counts as 0, so that the
// creases a distance field leaves along the middle of a shape round off and
// edges taper. Blocks and pipes, exact already, are left as they are.

uniform sampler2D u_shape;
uniform sampler2D u_background;

out vec4 o_shape;

const float WEIGHTS[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 shape = texelFetch(u_shape, p, 0);
    bool scenery = material_of(texelFetch(u_background, p, 0)) == M_SCENERY;
    float background = 0.0;
    float sprite = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            ivec2 q = clamp(p + ivec2(dx, dy), ivec2(0), ivec2(SCENE_WIDTH - 1, SCENE_HEIGHT - 1));
            float w = WEIGHTS[dx + 2] * WEIGHTS[dy + 2] / 256.0;
            vec4 neighbour = texelFetch(u_shape, q, 0);
            sprite += w * neighbour.w;
            if (scenery && material_of(texelFetch(u_background, q, 0)) == M_SCENERY) background += w * neighbour.y;
        }
    }
    if (scenery && shape.y > 0.0) shape.y = max(background, 1.0);
    if (shape.w > 0.0) shape.w = max(sprite, 1.0);
    o_shape = shape;
}
