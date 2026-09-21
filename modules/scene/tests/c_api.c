#include "nativekit_scene.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static nkscene_transform translated(float x) {
    nkscene_transform result = {0};
    result.matrix[0] = 1.0f;
    result.matrix[5] = 1.0f;
    result.matrix[10] = 1.0f;
    result.matrix[15] = 1.0f;
    result.matrix[12] = x;
    return result;
}

int main(void) {
    nkscene_scene scene = {0};
    assert(nkscene_scene_create(&scene) == NKS_OK);

    nkscene_geometry_id geometry = {0};
    assert(nkscene_geometry_create(scene, &geometry) == NKS_OK);
    const float positions[] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };
    const float normals[] = {
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
    };
    nkscene_vertex_stream streams[2] = {0};
    streams[0].struct_size = sizeof(streams[0]);
    streams[0].semantic = NKS_VERTEX_SEMANTIC_POSITION;
    streams[0].format = NKS_VERTEX_FORMAT_FLOAT32X3;
    streams[0].data = positions;
    streams[0].count = 3;
    streams[1].struct_size = sizeof(streams[1]);
    streams[1].semantic = NKS_VERTEX_SEMANTIC_NORMAL;
    streams[1].format = NKS_VERTEX_FORMAT_FLOAT32X3;
    streams[1].data = normals;
    streams[1].count = 3;
    nkscene_geometry_data stream_geometry = {0};
    stream_geometry.struct_size = sizeof(stream_geometry);
    stream_geometry.primitive_type = NKS_PRIMITIVE_TRIANGLES;
    stream_geometry.streams = streams;
    stream_geometry.stream_count = 2;
    assert(nkscene_geometry_set_data(scene, geometry, &stream_geometry) == NKS_OK);

    nkscene_image_id image = {0};
    nkscene_texture_id texture = {0};
    nkscene_sampler_id sampler = {0};
    nkscene_material_id material = {0};
    nkscene_camera_id camera = {0};
    nkscene_light_id light = {0};
    assert(nkscene_image_create(scene, &image) == NKS_OK);
    const uint8_t image_pixel[] = {255, 128, 64, 255};
    nkscene_image_data image_data = {0};
    image_data.struct_size = sizeof(image_data);
    image_data.width = 1;
    image_data.height = 1;
    image_data.format = NKS_IMAGE_FORMAT_RGBA8;
    image_data.mip_count = 1;
    image_data.data = image_pixel;
    image_data.data_size = sizeof(image_pixel);
    assert(nkscene_image_set_data(scene, image, &image_data) == NKS_OK);
    assert(nkscene_texture_create(scene, &texture) == NKS_OK);
    nkscene_texture_data texture_data = {0};
    texture_data.struct_size = sizeof(texture_data);
    texture_data.image = image;
    assert(nkscene_texture_set_data(scene, texture, &texture_data) == NKS_OK);
    assert(nkscene_sampler_create(scene, &sampler) == NKS_OK);
    nkscene_sampler_data sampler_data = {0};
    sampler_data.struct_size = sizeof(sampler_data);
    sampler_data.min_filter = NKS_SAMPLER_FILTER_LINEAR;
    sampler_data.mag_filter = NKS_SAMPLER_FILTER_LINEAR;
    sampler_data.wrap_u = NKS_SAMPLER_WRAP_REPEAT;
    sampler_data.wrap_v = NKS_SAMPLER_WRAP_REPEAT;
    sampler_data.wrap_w = NKS_SAMPLER_WRAP_REPEAT;
    sampler_data.max_anisotropy = 1.0f;
    assert(nkscene_sampler_set_data(scene, sampler, &sampler_data) == NKS_OK);
    assert(nkscene_material_create(scene, &material) == NKS_OK);
    nkscene_material_data material_data = {0};
    material_data.struct_size = sizeof(material_data);
    material_data.base_color[0] = 1.0f;
    material_data.base_color[1] = 0.8f;
    material_data.base_color[2] = 0.4f;
    material_data.base_color[3] = 1.0f;
    material_data.opacity = 1.0f;
    material_data.flags = NKS_MATERIAL_OPAQUE;
    material_data.roughness = 0.6f;
    material_data.alpha_mode = NKS_MATERIAL_ALPHA_OPAQUE;
    material_data.base_color_texture = texture;
    material_data.sampler = sampler;
    assert(nkscene_material_set_data(scene, material, &material_data) == NKS_OK);
    assert(nkscene_camera_create(scene, &camera) == NKS_OK);
    nkscene_camera_data camera_data = {0};
    camera_data.struct_size = sizeof(camera_data);
    camera_data.projection = NKS_CAMERA_PERSPECTIVE;
    camera_data.fov_y = 1.0f;
    camera_data.near_plane = 0.01f;
    camera_data.far_plane = 100.0f;
    assert(nkscene_camera_set_data(scene, camera, &camera_data) == NKS_OK);
    assert(nkscene_light_create(scene, &light) == NKS_OK);
    nkscene_light_data light_data = {0};
    light_data.struct_size = sizeof(light_data);
    light_data.type = NKS_LIGHT_DIRECTIONAL;
    light_data.color[0] = 1.0f;
    light_data.color[1] = 1.0f;
    light_data.color[2] = 1.0f;
    light_data.intensity = 2.0f;
    light_data.range = 10.0f;
    assert(nkscene_light_set_data(scene, light, &light_data) == NKS_OK);

    nkscene_transaction transaction = {0};
    assert(nkscene_transaction_begin(scene, &transaction) == NKS_OK);
    nkscene_occurrence_id group = {0};
    nkscene_occurrence_id first = {0};
    nkscene_occurrence_id second = {0};
    assert(nkscene_tx_create_occurrence(transaction, &group) == NKS_OK);
    assert(nkscene_tx_create_occurrence(transaction, &first) == NKS_OK);
    assert(nkscene_tx_create_occurrence(transaction, &second) == NKS_OK);
    assert(nkscene_tx_set_parent(transaction, first, group) == NKS_OK);
    assert(nkscene_tx_set_parent(transaction, second, group) == NKS_OK);
    const nkscene_transform first_transform = translated(-2.0f);
    const nkscene_transform second_transform = translated(3.0f);
    const nkscene_transform_update transform_updates[] = {
        {first, first_transform},
        {second, second_transform},
    };
    assert(nkscene_tx_set_transforms(transaction, transform_updates, 2) == NKS_OK);
    const nkscene_entity_id first_source = {42};
    assert(nkscene_tx_set_source_entity(transaction, first, first_source) == NKS_OK);
    assert(nkscene_tx_set_material(transaction, first, material) == NKS_OK);
    assert(nkscene_tx_set_camera(transaction, first, camera) == NKS_OK);
    assert(nkscene_tx_set_light(transaction, first, light) == NKS_OK);
    assert(nkscene_tx_set_name(transaction, first, "panda_link0") == NKS_OK);
    assert(nkscene_tx_set_entity_name(transaction, first_source, "RobotLink") == NKS_OK);
    assert(nkscene_transaction_commit(transaction) == NKS_OK);

    nkscene_snapshot snapshot = {0};
    assert(nkscene_scene_snapshot(scene, &snapshot) == NKS_OK);
    uint64_t count = 0;
    assert(nkscene_snapshot_get_occurrence_count(snapshot, &count) == NKS_OK);
    assert(count == 3);

    nkscene_snapshot_occurrence info = {0};
    info.struct_size = sizeof(info);
    int found_group = 0;
    int found_first = 0;
    int found_second = 0;
    for (uint64_t index = 0; index < count; ++index) {
        assert(nkscene_snapshot_get_occurrence(snapshot, index, &info) == NKS_OK);
        if (info.occurrence.value == group.value) {
            found_group = 1;
            assert(info.parent.value == 0);
            assert(info.visible != 0);
        } else if (info.occurrence.value == first.value) {
            found_first = 1;
            assert(info.parent.value == group.value);
            assert(info.world_transform.matrix[12] == -2.0f);
            assert(info.source.value == 42);
            assert(info.material.value == material.value);
            assert(info.camera.value == camera.value);
            assert(info.light.value == light.value);
        } else if (info.occurrence.value == second.value) {
            found_second = 1;
            assert(info.parent.value == group.value);
            assert(info.source.value == 0);
            assert(info.world_transform.matrix[12] == 3.0f);
        }
    }
    assert(found_group && found_first && found_second);

    const char *name = NULL;
    assert(nkscene_snapshot_get_name(snapshot, first, &name) == NKS_OK);
    assert(name != NULL);
    assert(strcmp(name, "panda_link0") == 0);
    assert(nkscene_snapshot_get_entity_name(snapshot, first_source, &name) == NKS_OK);
    assert(name != NULL);
    assert(strcmp(name, "RobotLink") == 0);
    assert(nkscene_snapshot_get_occurrence(snapshot, count, &info) ==
           NKS_ERROR_INVALID_ARGUMENT);

    nkscene_snapshot_occurrence_page page = {0};
    page.struct_size = sizeof(page);
    assert(nkscene_snapshot_get_occurrence_page(snapshot, 0, &page) == NKS_OK);
    assert(page.start_index == 0);
    assert(page.count == count);
    assert(page.occurrences[0].occurrence.value == group.value);
    assert(page.occurrences[1].parent.value == group.value);
    assert(nkscene_snapshot_get_occurrence_page(snapshot, count, &page) == NKS_OK);
    assert(page.count == 0);

    uint64_t source_count = 0;
    assert(nkscene_snapshot_get_source_occurrence_count(
               snapshot, first_source, &source_count) == NKS_OK);
    assert(source_count == 1);
    nkscene_occurrence_id source_occurrence = {0};
    assert(nkscene_snapshot_get_source_occurrence(
               snapshot, first_source, 0, &source_occurrence) == NKS_OK);
    assert(source_occurrence.value == first.value);
    assert(nkscene_snapshot_get_source_occurrence(
               snapshot, first_source, source_count, &source_occurrence) ==
           NKS_ERROR_INVALID_ARGUMENT);

    nkscene_snapshot_destroy(snapshot);
    nkscene_light_destroy(scene, light);
    nkscene_camera_destroy(scene, camera);
    nkscene_material_destroy(scene, material);
    nkscene_sampler_destroy(scene, sampler);
    nkscene_texture_destroy(scene, texture);
    nkscene_image_destroy(scene, image);
    nkscene_geometry_destroy(scene, geometry);
    nkscene_scene_destroy(scene);
    return 0;
}
