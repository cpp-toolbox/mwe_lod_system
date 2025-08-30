#include "utility/glm_printing/glm_printing.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/fwd.hpp>

// REMOVE this one day.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "graphics/draw_info/draw_info.hpp"
#include "graphics/input_graphics_sound_menu/input_graphics_sound_menu.hpp"

#include "input/glfw_lambda_callback_manager/glfw_lambda_callback_manager.hpp"
#include "input/input_state/input_state.hpp"

#include "utility/fixed_frequency_loop/fixed_frequency_loop.hpp"

#include "graphics/ui_render_suite_implementation/ui_render_suite_implementation.hpp"
#include "graphics/input_graphics_sound_menu/input_graphics_sound_menu.hpp"
#include "graphics/vertex_geometry/vertex_geometry.hpp"
#include "graphics/shader_standard/shader_standard.hpp"
#include "graphics/batcher/generated/batcher.hpp"
#include "graphics/shader_cache/shader_cache.hpp"
#include "graphics/fps_camera/fps_camera.hpp"
#include "graphics/window/window.hpp"
#include "graphics/colors/colors.hpp"
#include "graphics/ui/ui.hpp"
#include "graphics/drawer/drawer.hpp"

#include "system_logic/toolbox_engine/toolbox_engine.hpp"

#include "utility/unique_id_generator/unique_id_generator.hpp"
#include "utility/logger/logger.hpp"
#include "utility/timer/timer.hpp"

#include <iostream>

glm::vec2 get_ndc_mouse_pos1(GLFWwindow *window, double xpos, double ypos) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);

    return {(2.0f * xpos) / width - 1.0f, 1.0f - (2.0f * ypos) / height};
}

glm::vec2 aspect_corrected_ndc_mouse_pos1(const glm::vec2 &ndc_mouse_pos, float x_scale) {
    return {ndc_mouse_pos.x * x_scale, ndc_mouse_pos.y};
}

class Hud3D {
  private:
    Batcher &batcher;
    InputState &input_state;
    Configuration &configuration;
    FPSCamera &fps_camera;
    UIRenderSuiteImpl &ui_render_suite;
    Window &window;

    const glm::vec3 crosshair_color = colors::green;

    const std::string crosshair = R"(
--*--
--*--
*****
--*--
--*--
)";

    vertex_geometry::Rectangle crosshair_rect = vertex_geometry::Rectangle(glm::vec3(0), 0.1, 0.1);

    draw_info::IVPColor crosshair_ivpsc;

    int crosshair_batcher_object_id;

  public:
    Hud3D(Configuration &configuration, InputState &input_state, Batcher &batcher, FPSCamera &fps_camera,
          UIRenderSuiteImpl &ui_render_suite, Window &window)
        : batcher(batcher), input_state(input_state), configuration(configuration), fps_camera(fps_camera),
          ui_render_suite(ui_render_suite), window(window), ui(create_ui()) {}

    ConsoleLogger logger;

    UI ui;
    int fps_ui_element_id, pos_ui_element_id;
    float average_fps;

    UI create_ui() {
        UI hud_ui(0, batcher.absolute_position_with_colored_vertex_shader_batcher.object_id_generator);
        fps_ui_element_id = hud_ui.add_textbox(
            "FPS", vertex_geometry::create_rectangle_from_top_right(glm::vec3(1, 1, 0), 0.2, 0.2), colors::black);
        pos_ui_element_id = hud_ui.add_textbox(
            "POS", vertex_geometry::create_rectangle_from_bottom_left(glm::vec3(-1, -1, 0), 0.8, 0.4), colors::black);

        crosshair_batcher_object_id =
            batcher.absolute_position_with_colored_vertex_shader_batcher.object_id_generator.get_id();
        auto crosshair_ivp = vertex_geometry::text_grid_to_rect_grid(crosshair, crosshair_rect);
        std::vector<glm::vec3> cs(crosshair_ivp.xyz_positions.size(), crosshair_color);
        crosshair_ivpsc = draw_info::IVPColor(crosshair_ivp, cs, crosshair_batcher_object_id);

        return hud_ui;
    }

    void process_and_queue_render_hud_ui_elements() {

        if (configuration.get_value("graphics", "show_pos").value_or("off") == "on") {
            ui.unhide_textbox(pos_ui_element_id);
            ui.modify_text_of_a_textbox(pos_ui_element_id, vec3_to_string(fps_camera.transform.get_translation()));
        } else {
            ui.hide_textbox(pos_ui_element_id);
        }

        if (configuration.get_value("graphics", "show_fps").value_or("off") == "on") {
            std::ostringstream fps_stream;
            fps_stream << std::fixed << std::setprecision(1) << average_fps;
            ui.modify_text_of_a_textbox(fps_ui_element_id, fps_stream.str());
            ui.unhide_textbox(fps_ui_element_id);
        } else {
            ui.hide_textbox(fps_ui_element_id);
        }

        auto ndc_mouse_pos =
            get_ndc_mouse_pos1(window.glfw_window, input_state.mouse_position_x, input_state.mouse_position_y);
        auto acnmp = aspect_corrected_ndc_mouse_pos1(ndc_mouse_pos, window.width_px / (float)window.height_px);

        batcher.absolute_position_with_colored_vertex_shader_batcher.queue_draw(
            crosshair_batcher_object_id, crosshair_ivpsc.indices, crosshair_ivpsc.xyz_positions,
            crosshair_ivpsc.rgb_colors);

        process_and_queue_render_ui(acnmp, ui, ui_render_suite, input_state.get_just_pressed_key_strings(),
                                    input_state.is_just_pressed(EKey::BACKSPACE),
                                    input_state.is_just_pressed(EKey::ENTER),
                                    input_state.is_just_pressed(EKey::LEFT_MOUSE_BUTTON));
    }
};

draw_info::IndexedVertexPositions get_axes_ivp(float axis_length = 1.0f, float axis_thickness = 0.02f,
                                               float label_offset = 0.1f) {
    draw_info::IndexedVertexPositions axes_ivp;

    // Define axis endpoints
    glm::vec3 origin(0.0f);

    // Positive axes
    glm::vec3 x_pos(axis_length, 0, 0);
    glm::vec3 y_pos(0, axis_length, 0);
    glm::vec3 z_pos(0, 0, axis_length);

    // Negative axes
    glm::vec3 x_neg(-axis_length, 0, 0);
    glm::vec3 y_neg(0, -axis_length, 0);
    glm::vec3 z_neg(0, 0, -axis_length);

    // Generate arrows
    auto arrow_x_pos = vertex_geometry::generate_3d_arrow(origin, x_pos, 16, axis_thickness);
    auto arrow_y_pos = vertex_geometry::generate_3d_arrow(origin, y_pos, 16, axis_thickness);
    auto arrow_z_pos = vertex_geometry::generate_3d_arrow(origin, z_pos, 16, axis_thickness);

    auto arrow_x_neg = vertex_geometry::generate_3d_arrow(origin, x_neg, 16, axis_thickness);
    auto arrow_y_neg = vertex_geometry::generate_3d_arrow(origin, y_neg, 16, axis_thickness);
    auto arrow_z_neg = vertex_geometry::generate_3d_arrow(origin, z_neg, 16, axis_thickness);

    // Merge all arrows into one IVP
    vertex_geometry::merge_ivps(axes_ivp,
                                {arrow_x_pos, arrow_y_pos, arrow_z_pos, arrow_x_neg, arrow_y_neg, arrow_z_neg});

    // Generate labels using grid_font
    auto label_x = grid_font::get_text_geometry(
        "X", vertex_geometry::Rectangle(x_pos + glm::vec3(label_offset, 0, 0), 0.1f, 0.1f));
    auto label_y = grid_font::get_text_geometry(
        "Y", vertex_geometry::Rectangle(y_pos + glm::vec3(0, label_offset, 0), 0.1f, 0.1f));
    auto label_z = grid_font::get_text_geometry(
        "Z", vertex_geometry::Rectangle(z_pos + glm::vec3(0, 0, label_offset), 0.1f, 0.1f));

    auto label_x_neg = grid_font::get_text_geometry(
        "-X", vertex_geometry::Rectangle(x_neg - glm::vec3(label_offset, 0, 0), 0.1f, 0.1f));
    auto label_y_neg = grid_font::get_text_geometry(
        "-Y", vertex_geometry::Rectangle(y_neg - glm::vec3(0, label_offset, 0), 0.1f, 0.1f));
    auto label_z_neg = grid_font::get_text_geometry(
        "-Z", vertex_geometry::Rectangle(z_neg - glm::vec3(0, 0, label_offset), 0.1f, 0.1f));

    // Merge labels
    vertex_geometry::merge_ivps(axes_ivp, {label_x, label_y, label_z, label_x_neg, label_y_neg, label_z_neg});

    return axes_ivp;
}

template <typename T>
concept Indexable = requires(T t, std::size_t i) {
    { t.size() } -> std::convertible_to<std::size_t>;
    { t[i] };
};

template <Indexable Container> Container rotate(const Container &c, int shift) {
    if (c.size() == 0)
        return c;

    std::size_t n = c.size();
    shift = ((shift % static_cast<int>(n)) + static_cast<int>(n)) % static_cast<int>(n);

    Container result = c; // copy structure
    for (std::size_t i = 0; i < n; ++i) {
        result[i] = c[(i + shift) % n];
    }

    return result;
}

// NOTE: next step is we need to generalize a bit, to make this system work in a more general form we need to generalize
// a frustrum culler to a system which just tells us if an object is visible to the camera. we need to clean up the
// deletion logic a bit and also fix up the batcher logic to actually contain those changes and then try and bring that
// into the other project. then we can incorporate that into the visualizer thing and chek the usage percentage

int main() {

    ToolboxEngine tbx_engine("fps camera with geom",
                             {ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR,
                              ShaderType::ABSOLUTE_POSITION_WITH_COLORED_VERTEX},
                             {});

    tbx_engine.fps_camera.fov.add_observer([&](const float &new_value) {
        tbx_engine.shader_cache.set_uniform(ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR,
                                            ShaderUniformVariable::CAMERA_TO_CLIP,
                                            tbx_engine.fps_camera.get_projection_matrix());
    });

    tbx_engine::register_input_graphics_sound_config_handlers(tbx_engine.configuration, tbx_engine.fps_camera,
                                                              tbx_engine.main_loop);

    UIRenderSuiteImpl ui_render_suite(tbx_engine.batcher);
    Hud3D hud(tbx_engine.configuration, tbx_engine.input_state, tbx_engine.batcher, tbx_engine.fps_camera,
              ui_render_suite, tbx_engine.window);
    InputGraphicsSoundMenu input_graphics_sound_menu(tbx_engine.window, tbx_engine.input_state, tbx_engine.batcher,
                                                     tbx_engine.sound_system, tbx_engine.configuration);

    tbx_engine.shader_cache.set_uniform(ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR,
                                        ShaderUniformVariable::RGBA_COLOR, glm::vec4(colors::cyan, 1));

    auto background = vertex_geometry::Rectangle(glm::vec3(0.5, 0.5, 0), 0.4, 0.1);
    auto background_ivp = draw_info::IVPColor(background.get_ivs(), colors::grey);
    auto usage = draw_info::IVPColor(background.get_ivs(), colors::blue);

    auto cube = vertex_geometry::generate_cylinder();
    auto cube_ivp = draw_info::IndexedVertexPositions(cube.indices, cube.xyz_positions);

    auto axes = get_axes_ivp();

    vertex_geometry::NGon oct(32);
    auto rotated_oct_pts = rotate(oct.get_points(), 2);
    vertex_geometry::translate_vertices_in_place(rotated_oct_pts, glm::vec3(0, 0, 1));
    vertex_geometry::NGon rotated_oct(rotated_oct_pts);

    Drawer drawer(tbx_engine.fps_camera, tbx_engine.window.width_px, tbx_engine.window.height_px, {false, true, true},
                  [&](draw_info::IVPColor &ivpc) {
                      tbx_engine.batcher.absolute_position_with_colored_vertex_shader_batcher.queue_draw(ivpc);
                  });

    // Call once at program start
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    auto rand_float = [](float min_val, float max_val) {
        return min_val + (max_val - min_val) * (static_cast<float>(std::rand()) / RAND_MAX);
    };

    auto rand_int = [](int min_val, int max_val) { return min_val + (std::rand() % (max_val - min_val + 1)); };

    std::vector<draw_info::IndexedVertexPositions> ivps = {cube_ivp, axes};

    auto ivpn = vertex_geometry::generate_icosphere(2, 1);
    auto ball_ivp = draw_info::IndexedVertexPositions(ivpn.indices, ivpn.xyz_positions);

    for (int i = 0; i < 150; ++i) {
        // Random translation in cube of size 100
        float cube_size = 300;
        glm::vec3 translation(rand_float(-cube_size / 2, cube_size / 2), rand_float(-cube_size / 2, cube_size / 2),
                              rand_float(-cube_size / 2, cube_size / 2));

        // Random scale between 0.3 and 1 on all axes
        glm::vec3 scale(rand_float(0.3f, 1.0f), rand_float(0.3f, 1.0f), rand_float(0.3f, 1.0f));

        ball_ivp.transform.set_translation(translation);
        ball_ivp.transform.set_scale(scale); // <-- set random scale

        ivps.push_back(ball_ivp);
    }

    auto ivp_draw_func_id = drawer.register_draw_function([&](draw_info::IndexedVertexPositions &ivp) {
        tbx_engine.batcher.cwl_v_transformation_ubos_1024_with_solid_color_shader_batcher.queue_draw(ivp);
    });

    auto ivp_delete_func_id = drawer.register_delete_function([&](draw_info::IndexedVertexPositions &ivp) {
        tbx_engine.batcher.cwl_v_transformation_ubos_1024_with_solid_color_shader_batcher.delete_object(ivp);
    });

    for (auto ivp : ivps) {
        drawer.add_object(ivp, ivp_draw_func_id, ivp_delete_func_id);
    }

    tbx_engine.shader_cache.set_uniform(ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR,
                                        ShaderUniformVariable::CAMERA_TO_CLIP,
                                        tbx_engine.fps_camera.get_projection_matrix());

    tbx_engine.shader_cache.set_uniform(ShaderType::ABSOLUTE_POSITION_WITH_COLORED_VERTEX,
                                        ShaderUniformVariable::ASPECT_RATIO,
                                        glm::vec2(tbx_engine.window.height_px / (float)tbx_engine.window.width_px, 1));

    ConsoleLogger tick_logger;
    tick_logger.disable_all_levels();
    std::function<void(double)> tick = [&](double dt) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        tbx_engine.shader_cache.set_uniform(ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR,
                                            ShaderUniformVariable::CAMERA_TO_CLIP,
                                            tbx_engine.fps_camera.get_projection_matrix());

        tbx_engine.shader_cache.set_uniform(ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR,
                                            ShaderUniformVariable::WORLD_TO_CAMERA,
                                            tbx_engine.fps_camera.get_view_matrix());

        drawer.draw_all();

        float usage_percent = tbx_engine.batcher.cwl_v_transformation_ubos_1024_with_solid_color_shader_batcher.fsat
                                  .get_usage_percentage();
        // std::cout << "usage: " << usage_percent << std::endl;
        auto rect = vertex_geometry::create_rectangle_from_center_left(
            background.get_center_left(), background.width * usage_percent, background.height);

        auto rect_ivp = rect.get_ivs();
        usage.xyz_positions = rect_ivp.xyz_positions;
        usage.indices = rect_ivp.indices;
        // NOTE: sketch but we know xyz positions will always have same size (rect) so don't have to update rgb.
        usage.buffer_modification_tracker.just_modified();

        tbx_engine.batcher.absolute_position_with_colored_vertex_shader_batcher.queue_draw(background_ivp);
        tbx_engine.batcher.absolute_position_with_colored_vertex_shader_batcher.queue_draw(usage);

        tbx_engine::potentially_switch_between_menu_and_3d_view(tbx_engine.input_state, input_graphics_sound_menu,
                                                                tbx_engine.fps_camera, tbx_engine.window);

        hud.process_and_queue_render_hud_ui_elements();

        if (input_graphics_sound_menu.enabled) {
            input_graphics_sound_menu.process_and_queue_render_menu(tbx_engine.window, tbx_engine.input_state,
                                                                    ui_render_suite);
        } else {
            tbx_engine::config_x_input_state_x_fps_camera_processing(tbx_engine.fps_camera, tbx_engine.input_state,
                                                                     tbx_engine.configuration, dt);
        }

        tbx_engine.batcher.cwl_v_transformation_ubos_1024_with_solid_color_shader_batcher.upload_ltw_matrices();
        tbx_engine.batcher.cwl_v_transformation_ubos_1024_with_solid_color_shader_batcher.draw_everything();
        tbx_engine.batcher.absolute_position_with_colored_vertex_shader_batcher.draw_everything();

        tbx_engine.sound_system.play_all_sounds();

        glfwSwapBuffers(tbx_engine.window.glfw_window);
        glfwPollEvents();

        // tick_logger.tick();

        TemporalBinarySignal::process_all();
    };

    std::function<bool()> termination = [&]() { return glfwWindowShouldClose(tbx_engine.window.glfw_window); };

    std::function<void(IterationStats)> loop_stats_function = [&](IterationStats is) {
        hud.average_fps = is.measured_frequency_hz;
    };

    tbx_engine.main_loop.start(tick, termination, loop_stats_function);

    return 0;
}
