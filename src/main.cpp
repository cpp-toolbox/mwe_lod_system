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

#include "system_logic/toolbox_engine/toolbox_engine.hpp"

#include "utility/unique_id_generator/unique_id_generator.hpp"
#include "utility/logger/logger.hpp"

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

int main() {

    ToolboxEngine tbx_engine("fps camera with geom");

    tbx_engine.fps_camera.fov.add_observer([&](const float &new_value) {
        tbx_engine.shader_cache.set_uniform(
            ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR, ShaderUniformVariable::CAMERA_TO_CLIP,
            tbx_engine.fps_camera.get_projection_matrix(tbx_engine.window.width_px, tbx_engine.window.height_px));
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

    GLFWLambdaCallbackManager glcm = tbx_engine::create_default_glcm_for_input_and_camera(
        tbx_engine.input_state, tbx_engine.fps_camera, tbx_engine.window, tbx_engine.shader_cache);

    auto torus = vertex_geometry::generate_torus();
    tbx_engine.batcher.cwl_v_transformation_ubos_1024_with_solid_color_shader_batcher.tag_id(torus);

    tbx_engine.shader_cache.set_uniform(
        ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR, ShaderUniformVariable::CAMERA_TO_CLIP,
        tbx_engine.fps_camera.get_projection_matrix(tbx_engine.window.width_px, tbx_engine.window.height_px));

    tbx_engine.shader_cache.set_uniform(ShaderType::ABSOLUTE_POSITION_WITH_COLORED_VERTEX,
                                        ShaderUniformVariable::ASPECT_RATIO,
                                        glm::vec2(tbx_engine.window.height_px / (float)tbx_engine.window.width_px, 1));

    ConsoleLogger tick_logger;
    tick_logger.disable_all_levels();
    std::function<void(double)> tick = [&](double dt) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        tbx_engine.shader_cache.set_uniform(
            ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR, ShaderUniformVariable::CAMERA_TO_CLIP,
            tbx_engine.fps_camera.get_projection_matrix(tbx_engine.window.width_px, tbx_engine.window.height_px));

        tbx_engine.shader_cache.set_uniform(ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR,
                                            ShaderUniformVariable::WORLD_TO_CAMERA,
                                            tbx_engine.fps_camera.get_view_matrix());

        tbx_engine.batcher.cwl_v_transformation_ubos_1024_with_solid_color_shader_batcher.queue_draw(torus);

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
