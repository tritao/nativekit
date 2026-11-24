@cs step_cs
layout(local_size_x=64) in;
struct particle { vec4 position_velocity; };
layout(binding=0) buffer particles { particle items[]; };
layout(std140, binding=0) uniform step_params { float dt; };

void main() {
    uint index = gl_GlobalInvocationID.x;
    items[index].position_velocity.xy += items[index].position_velocity.zw * dt;
}
@end

@program step step_cs
