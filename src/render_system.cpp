// internal
#include "render_system.hpp"
#include <GLFW/glfw3.h>
#include <SDL.h>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "common.hpp"
#include "components.hpp"
#include "tiny_ecs_registry.hpp"

#include "distort.hpp"

extern DistortToggle toggle;

void RenderSystem::renderTextBulk(std::vector<TextRenderRequest>& requests)
{
    glUseProgram(m_font_shaderProgram);
    glBindVertexArray(m_font_VAO);

    for (const auto& request : requests)
    {
        GLint textColor_location = glGetUniformLocation(m_font_shaderProgram, "textColor");
        glUniform3f(textColor_location, request.color.x, request.color.y, request.color.z);

        GLint transformLoc = glGetUniformLocation(m_font_shaderProgram, "transform");
        glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(request.transform));

        float currentX = request.x;
        
        for (const char c : request.text)
        {
            Character ch = m_ftCharacters[c];

            float xpos = currentX + ch.Bearing.x * request.scale;
            float ypos = request.y - (ch.Size.y - ch.Bearing.y) * request.scale;

            float w = ch.Size.x * request.scale;
            float h = ch.Size.y * request.scale;

            float vertices[6][4] = {
                { xpos,     ypos + h,   0.0f, 0.0f },
                { xpos,     ypos,       0.0f, 1.0f },
                { xpos + w, ypos,       1.0f, 1.0f },

                { xpos,     ypos + h,   0.0f, 0.0f },
                { xpos + w, ypos,       1.0f, 1.0f },
                { xpos + w, ypos + h,   1.0f, 0.0f }
            };

            glBindTexture(GL_TEXTURE_2D, ch.TextureID);

            glBindBuffer(GL_ARRAY_BUFFER, m_font_VBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            glDrawArrays(GL_TRIANGLES, 0, 6);

            currentX += (ch.Advance >> 6) * request.scale;
        }
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void RenderSystem::updateAnimations(float elapsed_ms) {
    float elapsed_seconds = elapsed_ms / 1000.f;
    
    for (auto entity : registry.animations.entities) {
        Animation& anim = registry.animations.get(entity);

        Motion motion;
        if (registry.enemyMotions.has(entity)) {
            motion = registry.enemyMotions.get(entity);
        }
        else if (registry.wallMotions.has(entity)) {
            motion = registry.wallMotions.get(entity);
        }
        else if (registry.projectileMotions.has(entity)) {
            motion = registry.projectileMotions.get(entity);
        }
        else {
            motion = registry.motions.get(entity);
        }
        if (!anim.is_playing) continue;

		if (
			(registry.players.has(entity) && (motion.velocity.x != 0 || motion.velocity.y != 0)) ||
			(registry.enemies.has(entity) && registry.enemies.get(entity).enemyState != EnemyState::ROAMING)
		) {
			anim.current_time += elapsed_seconds;
			if (anim.current_time >= anim.frame_time) {
				anim.current_time = 0.f;
				anim.current_frame++;
				
				if (anim.current_frame >= anim.num_frames) {
					if (anim.loop) {
						anim.current_frame = 0;
					} else {
						anim.current_frame = anim.num_frames - 1;
						anim.is_playing = false;
					}
				}
			}
		} else {
			anim.current_time = 0.f;
			anim.current_frame = 0;
		}
    }
}


void RenderSystem::drawTexturedMeshWithAnim(Entity entity, const mat3& projection, const Animation& anim) {
	assert(registry.renderRequests.has(entity));
    const RenderRequest& render_request = registry.renderRequests.get(entity);
    const ivec2& tex_size = texture_dimensions[(GLuint)render_request.used_texture];

    float frame_width = float(anim.sprite_width) / tex_size.x;
    float frame_x = (anim.current_frame * anim.sprite_width) / float(tex_size.x);

    std::vector<TexturedVertex> textured_vertices(4);
    textured_vertices[0].position = { -1.f/2, +1.f/2, 0.f };
    textured_vertices[1].position = { +1.f/2, +1.f/2, 0.f };
    textured_vertices[2].position = { +1.f/2, -1.f/2, 0.f };
    textured_vertices[3].position = { -1.f/2, -1.f/2, 0.f };
    textured_vertices[0].texcoord = { frame_x, 1.f };
    textured_vertices[1].texcoord = { frame_x + frame_width, 1.f };
    textured_vertices[2].texcoord = { frame_x + frame_width, 0.f };
    textured_vertices[3].texcoord = { frame_x, 0.f };

    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[(GLuint)GEOMETRY_BUFFER_ID::SPRITE]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(TexturedVertex) * textured_vertices.size(), textured_vertices.data());
}


void RenderSystem::drawTexturedMesh(Entity entity, const mat3 &projection)
{
    Motion motion;
    if (registry.enemyMotions.has(entity)) {
        motion = registry.enemyMotions.get(entity);
    }
    else if (registry.wallMotions.has(entity)) {
        motion = registry.wallMotions.get(entity);
    }
    else if (registry.projectileMotions.has(entity)) {
        motion = registry.projectileMotions.get(entity);
    }
    else {
        motion = registry.motions.get(entity);
    }
	Transform transform;
	transform.translate(motion.position);
	
	assert(registry.renderRequests.has(entity));
	const RenderRequest &render_request = registry.renderRequests.get(entity);

    if (fabsf(motion.angle) < (M_PI/2) && !(registry.projectiles.has(entity))) {
        transform.rotate(motion.angle - M_PI);
        transform.scale(vec2(-motion.scale.x, motion.scale.y));
    }
    else {
        transform.rotate(motion.angle);
        transform.scale(motion.scale);
    }

	const GLuint used_effect_enum = (GLuint)render_request.used_effect;
	assert(used_effect_enum != (GLuint)EFFECT_ASSET_ID::EFFECT_COUNT);
	const GLuint program = (GLuint)effects[used_effect_enum];

	// Setting shaders
	glUseProgram(program);
	gl_has_errors();

	assert(render_request.used_geometry != GEOMETRY_BUFFER_ID::GEOMETRY_COUNT);
	const GLuint vbo = vertex_buffers[(GLuint)render_request.used_geometry];
	const GLuint ibo = index_buffers[(GLuint)render_request.used_geometry];

	// Setting vertex and index buffers
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	gl_has_errors();

	// Input data location as in the vertex buffer
	if (render_request.used_effect == EFFECT_ASSET_ID::TEXTURED)
	{
		GLint in_position_loc = glGetAttribLocation(program, "in_position");
		GLint in_texcoord_loc = glGetAttribLocation(program, "in_texcoord");
		gl_has_errors();
		assert(in_texcoord_loc >= 0);

		glEnableVertexAttribArray(in_position_loc);
		glVertexAttribPointer(in_position_loc, 3, GL_FLOAT, GL_FALSE,
							  sizeof(TexturedVertex), (void *)0);
		gl_has_errors();

		glEnableVertexAttribArray(in_texcoord_loc);
		glVertexAttribPointer(
			in_texcoord_loc, 2, GL_FLOAT, GL_FALSE, sizeof(TexturedVertex),
			(void *)sizeof(
				vec3)); // note the stride to skip the preceeding vertex position

		// Enabling and binding texture to slot 0
		glActiveTexture(GL_TEXTURE0);
		gl_has_errors();

		assert(registry.renderRequests.has(entity));
		GLuint texture_id =
			texture_gl_handles[(GLuint)registry.renderRequests.get(entity).used_texture];

		glBindTexture(GL_TEXTURE_2D, texture_id);
		gl_has_errors();
	}
	else
	{
		assert(false && "Type of render request not supported");
	}

	// Getting uniform locations for glUniform* calls
	GLint color_uloc = glGetUniformLocation(program, "fcolor");
	const vec3 color = registry.colors.has(entity) ? registry.colors.get(entity) : vec3(1);
	glUniform3fv(color_uloc, 1, (float *)&color);
	gl_has_errors();

	// Get number of indices from index buffer, which has elements uint16_t
	GLint size = 0;
	glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
	gl_has_errors();

	GLsizei num_indices = size / sizeof(uint16_t);
	// GLsizei num_triangles = num_indices / 3;

	GLint currProgram;
	glGetIntegerv(GL_CURRENT_PROGRAM, &currProgram);
	// Setting uniform values to the currently bound program
	GLuint transform_loc = glGetUniformLocation(currProgram, "transform");
	glUniformMatrix3fv(transform_loc, 1, GL_FALSE, (float *)&transform.mat);
	GLuint projection_loc = glGetUniformLocation(currProgram, "projection");
	glUniformMatrix3fv(projection_loc, 1, GL_FALSE, (float *)&projection);
	gl_has_errors();
	// Drawing of num_indices/3 triangles specified in the index buffer
	glDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, nullptr);
	gl_has_errors();
}

// draw the intermediate texture to the screen, with some distortion to simulate
// water
void RenderSystem::drawToScreen()
{
	// Setting shaders
	// get the water texture, sprite mesh, and program
	glUseProgram(effects[(GLuint)EFFECT_ASSET_ID::WATER]);

	GLuint distortion_on = glGetUniformLocation(effects[(GLuint)EFFECT_ASSET_ID::WATER], "distort_on");

    if (toggle == DISTORT_ON) {
        glUniform1i(distortion_on, 1);  
    } else {
        glUniform1i(distortion_on, 0); 
    }

	gl_has_errors();
	// Clearing backbuffer
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDepthRange(0, 10);
	glClearColor(0.f, 0, 0, 1.0);
	glClearDepth(1.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	gl_has_errors();
	// Enabling alpha channel for textures
	glDisable(GL_BLEND);
	// glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);

	// Draw the screen texture on the quad geometry
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]);
	glBindBuffer(
		GL_ELEMENT_ARRAY_BUFFER,
		index_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]); // Note, GL_ELEMENT_ARRAY_BUFFER associates
																	 // indices to the bound GL_ARRAY_BUFFER
	gl_has_errors();
	const GLuint water_program = effects[(GLuint)EFFECT_ASSET_ID::WATER];
	// Set clock
	GLuint time_uloc = glGetUniformLocation(water_program, "time");
	GLuint dead_timer_uloc = glGetUniformLocation(water_program, "darken_screen_factor");
	GLuint light_up_uloc = glGetUniformLocation(water_program, "light_up");
	glUniform1f(time_uloc, (float)(glfwGetTime() * 10.0f));
	ScreenState &screen = registry.screenStates.get(screen_state_entity);
	glUniform1f(dead_timer_uloc, screen.darken_screen_factor);
	gl_has_errors();
	// Set the vertex position and vertex texture coordinates (both stored in the
	// same VBO)

	// Set high score flash value
    float light_up_amount = 0.f;
    if (registry.lightUps.has(screen_state_entity)) {
        LightUp& lightUp = registry.lightUps.get(screen_state_entity);
        light_up_amount = lightUp.timer / 1.5f;
    }
    glUniform1f(light_up_uloc, light_up_amount);

	GLint in_position_loc = glGetAttribLocation(water_program, "in_position");
	glEnableVertexAttribArray(in_position_loc);
	glVertexAttribPointer(in_position_loc, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void *)0);
	gl_has_errors();

	// Bind our texture in Texture Unit 0
	glActiveTexture(GL_TEXTURE0);

	glBindTexture(GL_TEXTURE_2D, off_screen_render_buffer_color);
	gl_has_errors();
	// Draw
	glDrawElements(
		GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,
		nullptr); // one triangle = 3 vertices; nullptr indicates that there is
				  // no offset from the bound index buffer
	gl_has_errors();
}

// Render our game world
// http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-14-render-to-texture/
void RenderSystem::draw(float elapsed_ms, bool isPaused)
{
	// Getting size of window
	int w, h;
	glfwGetFramebufferSize(window, &w, &h); // Note, this will be 2x the resolution given to glfwCreateWindow on retina displays
	glBindVertexArray(vao);

	// First render to the custom framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer);
	gl_has_errors();
	// Clearing backbuffer
	glViewport(0, 0, w, h);
	glDepthRange(0.00001, 10);

	glClearColor(0.75f, 0.75f, 0.75f, 1.0f); // black space background

	glClearDepth(10.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST); // native OpenGL does not work with a depth buffer
							  // and alpha blending, one would have to sort
							  // sprites back to front
	gl_has_errors();


	/* mat3 projection_2D = createProjectionMatrix(); */
	mat3 projection_2D = createCameraMatrix();

    if (!isPaused) {
        updateAnimations(elapsed_ms);
    }

	// Draw all textured meshes that have a position and size component
    const ScreenState& ss = registry.screenStates.get(screen_state_entity);
    if (ss.activeScreen == (int)SCREEN_ID::MAIN_MENU) {
        drawMainMenu();
    }
	else if (ss.activeScreen == (int)SCREEN_ID::TUTORIAL_SCREEN) {
		drawTutorial();
    }
    else if (ss.activeScreen == (int)SCREEN_ID::GAME_SCREEN || ss.activeScreen == (int) SCREEN_ID::PAUSE_SCREEN) {

        drawGameBackground();
        drawSpaceship();
        drawFloor();

        for (Entity entity : registry.renderRequests.entities)
        {
            if (registry.clickables.has(entity) || registry.players.has(entity) || entity == hoverEntity) 
                continue;
            // Note, its not very efficient to access elements indirectly via the entity
            // albeit iterating through all Sprites in sequence. A good point to optimize
                
            if (registry.animations.has(entity)) {
                drawTexturedMeshWithAnim(entity, projection_2D, registry.animations.get(entity));
            }
            drawTexturedMesh(entity, projection_2D);
        }
        if (LIGHT_SYSTEM_TOGGLE) {
            lightScreen();
        }
        // Draw player AFTER shadow has been cast so it is not shaded
        Entity player = registry.players.entities[0];
        drawTexturedMeshWithAnim(player, projection_2D, registry.animations.get(player));
        drawTexturedMesh(player, projection_2D);
        
        glBindVertexArray(vao);
    }
    else if (ss.activeScreen == (int) SCREEN_ID::DEATH_SCREEN) {
        drawDeathScreen();
    }
    else if (ss.activeScreen == (int) SCREEN_ID::WIN_SCREEN) {
        drawWinScreen();
    }

	// Truely render to the screen
	drawToScreen();

    if (ss.activeScreen == (int)SCREEN_ID::GAME_SCREEN)
	{
		// Render text
		glBindVertexArray(0);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glViewport(0, 0, w, h);

		int w, h;
		glfwGetWindowSize(window, &w, &h);

		std::vector<TextRenderRequest> texts = {};

		for (Entity entity: registry.texts.entities) {
			Text &text = registry.texts.get(entity);
			texts.emplace_back(text.text, text.position.x, h - text.position.y, text.scale, text.color, glm::mat4(4.0f));
		}
		
		if (!texts.empty()) {
			renderTextBulk(texts);
		}
		if (mouseGestures.isToggled) {
			drawMouseGestures();
		}
	}

    else if (ss.activeScreen == (int) SCREEN_ID::PAUSE_SCREEN) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		int w, h;
		glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        drawPauseMenu();
    }

	// flicker-free display with a double buffer
	glfwSwapBuffers(window);
	gl_has_errors();
}



void RenderSystem::drawMouseGestures() {
    glUseProgram(ges_shaderProgram);
    gl_has_errors();
    glUniform1f(glGetUniformLocation(ges_shaderProgram, "thickness"), 4.0f);
    glBindVertexArray(ges_VAO);
    auto &path = mouseGestures.renderPath;
    
    if (mouseGestures.isHeld && !mouseGestures.gesturePath.empty()) {
        std::vector<vec2> expandedPath;
        std::vector<float> sides;
        
        for (const auto& point : path) {
            expandedPath.push_back(point);
            expandedPath.push_back(point);
            sides.push_back(-1.0f);
            sides.push_back(1.0f);
        }

        glBindBuffer(GL_ARRAY_BUFFER, ges_VBO);
        glBufferData(GL_ARRAY_BUFFER, expandedPath.size() * sizeof(vec2), expandedPath.data(), GL_DYNAMIC_DRAW);
        gl_has_errors();

        GLuint sideVBO;
        glGenBuffers(1, &sideVBO);
        glBindBuffer(GL_ARRAY_BUFFER, sideVBO);
        glBufferData(GL_ARRAY_BUFFER, sides.size() * sizeof(float), sides.data(), GL_DYNAMIC_DRAW);
        gl_has_errors();

        glBindBuffer(GL_ARRAY_BUFFER, ges_VBO);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, sideVBO);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, expandedPath.size());
        glDeleteBuffers(1, &sideVBO);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl_has_errors();
}

void RenderSystem::drawMainMenu() {

    // Draw buttons first since drawing front to back
    // Setting shaders
	// get the water texture, sprite mesh, and program
	glUseProgram(effects[(GLuint)EFFECT_ASSET_ID::WATER]);
	gl_has_errors();
	// Draw the screen texture on the quad geometry
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]);
	glBindBuffer(
		GL_ELEMENT_ARRAY_BUFFER,
		index_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]); // Note, GL_ELEMENT_ARRAY_BUFFER associates
																	 // indices to the bound GL_ARRAY_BUFFER
	gl_has_errors();
	const GLuint water_program = effects[(GLuint)EFFECT_ASSET_ID::WATER];
	// Set clock
	GLuint time_uloc = glGetUniformLocation(water_program, "time");
	GLuint dead_timer_uloc = glGetUniformLocation(water_program, "darken_screen_factor");
	glUniform1f(time_uloc, (float)(glfwGetTime() * 10.0f));
	ScreenState &screen = registry.screenStates.get(screen_state_entity);
	glUniform1f(dead_timer_uloc, screen.darken_screen_factor);
	gl_has_errors();
	// Set the vertex position and vertex texture coordinates (both stored in the
	// same VBO)
	GLint in_position_loc = glGetAttribLocation(water_program, "in_position");
	glEnableVertexAttribArray(in_position_loc);
	glVertexAttribPointer(in_position_loc, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void *)0);
	gl_has_errors();

	// Bind our texture in Texture Unit 0
	glActiveTexture(GL_TEXTURE0);

	glBindTexture(GL_TEXTURE_2D, mainMenuTexture);
	gl_has_errors();
	// Draw
	glDrawElements(
		GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,
		nullptr); // one triangle = 3 vertices; nullptr indicates that there is
				  // no offset from the bound index buffer
    drawButtons();
	gl_has_errors();

}

void RenderSystem::drawTutorial() {

	// Draw buttons first since drawing front to back
	// Setting shaders
	// get the water texture, sprite mesh, and program
	glUseProgram(effects[(GLuint)EFFECT_ASSET_ID::WATER]);
	gl_has_errors();
	// Draw the screen texture on the quad geometry
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]);
	glBindBuffer(
		GL_ELEMENT_ARRAY_BUFFER,
		index_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]); // Note, GL_ELEMENT_ARRAY_BUFFER associates
	// indices to the bound GL_ARRAY_BUFFER
	gl_has_errors();
	const GLuint water_program = effects[(GLuint)EFFECT_ASSET_ID::WATER];
	// Set clock
	GLuint time_uloc = glGetUniformLocation(water_program, "time");
	GLuint dead_timer_uloc = glGetUniformLocation(water_program, "darken_screen_factor");
	glUniform1f(time_uloc, (float)(glfwGetTime() * 10.0f));
	ScreenState& screen = registry.screenStates.get(screen_state_entity);
	glUniform1f(dead_timer_uloc, screen.darken_screen_factor);
	gl_has_errors();
	// Set the vertex position and vertex texture coordinates (both stored in the
	// same VBO)
	GLint in_position_loc = glGetAttribLocation(water_program, "in_position");
	glEnableVertexAttribArray(in_position_loc);
	glVertexAttribPointer(in_position_loc, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void*)0);
	gl_has_errors();

	// Bind our texture in Texture Unit 0
	glActiveTexture(GL_TEXTURE0);

	glBindTexture(GL_TEXTURE_2D, tutorialTexture);
	gl_has_errors();
	// Draw
	glDrawElements(
		GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,
		nullptr); // one triangle = 3 vertices; nullptr indicates that there is
	// no offset from the bound index buffer

	gl_has_errors();
}

void RenderSystem::drawButtons() {
    mat3 projection_2D = createProjectionMatrix();
    bool anyButtonHoveredOver = false;
    for (Entity e : registry.clickables.entities) {
        Clickable c = registry.clickables.get(e);
        if (!c.isActive) {
            continue;
        }
        if (c.isCurrentlyHoveredOver) {
            anyButtonHoveredOver = true;
            if (!registry.renderRequests.has(hoverEntity)) {
                registry.renderRequests.insert(hoverEntity, {
                        TEXTURE_ASSET_ID::BUTTON_BORDER,
                        EFFECT_ASSET_ID::TEXTURED,
                        GEOMETRY_BUFFER_ID::UI_COMPONENT
                        });
            }
            drawTexturedMesh(hoverEntity, projection_2D);
        }
        if (!registry.renderRequests.has(e)) {
            registry.renderRequests.insert(e, {
                    static_cast<TEXTURE_ASSET_ID>(c.textureID),
                    EFFECT_ASSET_ID::TEXTURED,
                    GEOMETRY_BUFFER_ID::UI_COMPONENT
                    });
        }
        drawTexturedMesh(e, projection_2D);
    }

    if (!anyButtonHoveredOver && registry.renderRequests.has(hoverEntity)){
        registry.renderRequests.remove(hoverEntity);
    }

}

mat3 RenderSystem::createProjectionMatrix()
{
	// Fake projection matrix, scales with respect to window coordinates
	float left = 0.f;
	float top = 0.f;

	gl_has_errors();
	float right = (float) window_width_px;
	float bottom = (float) window_height_px;

	float sx = 2.f / (right - left);
	float sy = 2.f / (top - bottom);
	float tx = -(right + left) / (right - left);
	float ty = -(top + bottom) / (top - bottom);
	return {{sx, 0.f, 0.f}, {0.f, sy, 0.f}, {tx, ty, 1.f}};
}

mat3 RenderSystem::createCameraMatrix()
{
	// Fake projection matrix, scales with respect to window coordinates

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    Entity p = registry.players.entities[0];
    Motion& m = registry.motions.get(p);

	float left = m.position.x - w/2;
	float top = m.position.y - h/2;

	gl_has_errors();
	float right = m.position.x + w/2;
	float bottom = m.position.y + h/2;

	float sx = 2.f / (right - left);
	float sy = 2.f / (top - bottom);
	float tx = -(right + left) / (right - left);
	float ty = -(top + bottom) / (top - bottom);
	return {{sx, 0.f, 0.f}, {0.f, sy, 0.f}, {tx, ty, 1.f}};
}

void RenderSystem::setActiveScreen(int activeScreen) {
    ScreenState& ss = registry.screenStates.get(screen_state_entity);
    ss.activeScreen = activeScreen;
}

int RenderSystem::getActiveScreen() const {
    ScreenState& ss = registry.screenStates.get(screen_state_entity);
    return ss.activeScreen;
}

void RenderSystem::drawPauseMenu() {
	// Draw buttons first since drawing front to back
	// Setting shaders
	// get the water texture, sprite mesh, and program
	glUseProgram(effects[(GLuint)EFFECT_ASSET_ID::WATER]);
	gl_has_errors();
	// Draw the screen texture on the quad geometry
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]);
	glBindBuffer(
		GL_ELEMENT_ARRAY_BUFFER,
		index_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]); // Note, GL_ELEMENT_ARRAY_BUFFER associates
	// indices to the bound GL_ARRAY_BUFFER
	gl_has_errors();
	const GLuint water_program = effects[(GLuint)EFFECT_ASSET_ID::WATER];
	// Set clock
	GLuint time_uloc = glGetUniformLocation(water_program, "time");
	GLuint dead_timer_uloc = glGetUniformLocation(water_program, "darken_screen_factor");
	glUniform1f(time_uloc, (float)(glfwGetTime() * 10.0f));
	ScreenState& screen = registry.screenStates.get(screen_state_entity);
	glUniform1f(dead_timer_uloc, screen.darken_screen_factor);
	gl_has_errors();
	// Set the vertex position and vertex texture coordinates (both stored in the
	// same VBO)
	GLint in_position_loc = glGetAttribLocation(water_program, "in_position");
	glEnableVertexAttribArray(in_position_loc);
	glVertexAttribPointer(in_position_loc, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void*)0);
	gl_has_errors();

	// Bind our texture in Texture Unit 0
	glActiveTexture(GL_TEXTURE0);

	glBindTexture(GL_TEXTURE_2D, pauseMenuTexture);
	gl_has_errors();
	// Draw
	glDrawElements(
		GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,
		nullptr); // one triangle = 3 vertices; nullptr indicates that there is
	// no offset from the bound index buffer

    drawButtons();

	gl_has_errors();

}

void RenderSystem::drawDeathScreen() {
	// Draw buttons first since drawing front to back
	// Setting shaders
	// get the water texture, sprite mesh, and program
	glUseProgram(effects[(GLuint)EFFECT_ASSET_ID::WATER]);
	gl_has_errors();
	// Draw the screen texture on the quad geometry
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]);
	glBindBuffer(
		GL_ELEMENT_ARRAY_BUFFER,
		index_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]); // Note, GL_ELEMENT_ARRAY_BUFFER associates
	// indices to the bound GL_ARRAY_BUFFER
	gl_has_errors();
	const GLuint water_program = effects[(GLuint)EFFECT_ASSET_ID::WATER];
	// Set clock
	GLuint time_uloc = glGetUniformLocation(water_program, "time");
	GLuint dead_timer_uloc = glGetUniformLocation(water_program, "darken_screen_factor");
	glUniform1f(time_uloc, (float)(glfwGetTime() * 10.0f));
	ScreenState& screen = registry.screenStates.get(screen_state_entity);
	glUniform1f(dead_timer_uloc, screen.darken_screen_factor);
	gl_has_errors();
	// Set the vertex position and vertex texture coordinates (both stored in the
	// same VBO)
	GLint in_position_loc = glGetAttribLocation(water_program, "in_position");
	glEnableVertexAttribArray(in_position_loc);
	glVertexAttribPointer(in_position_loc, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void*)0);
	gl_has_errors();

	// Bind our texture in Texture Unit 0
	glActiveTexture(GL_TEXTURE0);

	glBindTexture(GL_TEXTURE_2D, deathScreenTexture);
	gl_has_errors();
	// Draw
	glDrawElements(
		GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,
		nullptr); // one triangle = 3 vertices; nullptr indicates that there is
	// no offset from the bound index buffer

    drawButtons();

	gl_has_errors();

}

void RenderSystem::drawWinScreen() {
	// Draw buttons first since drawing front to back
	// Setting shaders
	// get the water texture, sprite mesh, and program
	glUseProgram(effects[(GLuint)EFFECT_ASSET_ID::WATER]);
	gl_has_errors();
	// Draw the screen texture on the quad geometry
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]);
	glBindBuffer(
		GL_ELEMENT_ARRAY_BUFFER,
		index_buffers[(GLuint)GEOMETRY_BUFFER_ID::SCREEN_TRIANGLE]); // Note, GL_ELEMENT_ARRAY_BUFFER associates
	// indices to the bound GL_ARRAY_BUFFER
	gl_has_errors();
	const GLuint water_program = effects[(GLuint)EFFECT_ASSET_ID::WATER];
	// Set clock
	GLuint time_uloc = glGetUniformLocation(water_program, "time");
	GLuint dead_timer_uloc = glGetUniformLocation(water_program, "darken_screen_factor");
	glUniform1f(time_uloc, (float)(glfwGetTime() * 10.0f));
	ScreenState& screen = registry.screenStates.get(screen_state_entity);
	glUniform1f(dead_timer_uloc, screen.darken_screen_factor);
	gl_has_errors();
	// Set the vertex position and vertex texture coordinates (both stored in the
	// same VBO)
	GLint in_position_loc = glGetAttribLocation(water_program, "in_position");
	glEnableVertexAttribArray(in_position_loc);
	glVertexAttribPointer(in_position_loc, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void*)0);
	gl_has_errors();

	// Bind our texture in Texture Unit 0
	glActiveTexture(GL_TEXTURE0);

	glBindTexture(GL_TEXTURE_2D, winScreenTexture);
	gl_has_errors();
	// Draw
	glDrawElements(
		GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,
		nullptr); // one triangle = 3 vertices; nullptr indicates that there is
	// no offset from the bound index buffer

    drawButtons();

	gl_has_errors();

}

void RenderSystem::drawGameBackground() {
    const GLuint program = effects[(GLuint)EFFECT_ASSET_ID::TEXTURED];
	glUseProgram(program);
	gl_has_errors();
	// Draw the screen texture on the quad geometry
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[(GLuint)GEOMETRY_BUFFER_ID::UI_COMPONENT]);
	glBindBuffer(
		GL_ELEMENT_ARRAY_BUFFER,
		index_buffers[(GLuint)GEOMETRY_BUFFER_ID::UI_COMPONENT]); // Note, GL_ELEMENT_ARRAY_BUFFER associates
	// indices to the bound GL_ARRAY_BUFFER
	gl_has_errors();
    GLint in_position_loc = glGetAttribLocation(program, "in_position");
    GLint in_texcoord_loc = glGetAttribLocation(program, "in_texcoord");
    gl_has_errors();
    assert(in_texcoord_loc >= 0);

    glEnableVertexAttribArray(in_position_loc);
    glVertexAttribPointer(in_position_loc, 3, GL_FLOAT, GL_FALSE,
                          sizeof(TexturedVertex), (void *)0);
    gl_has_errors();

    glEnableVertexAttribArray(in_texcoord_loc);
    glVertexAttribPointer(
        in_texcoord_loc, 2, GL_FLOAT, GL_FALSE, sizeof(TexturedVertex),
        (void *)sizeof(
            vec3)); // note the stride to skip the preceeding vertex position

    // Enabling and binding texture to slot 0
    glActiveTexture(GL_TEXTURE0);
    gl_has_errors();

    glBindTexture(GL_TEXTURE_2D, gameBackgroundTexture);
    gl_has_errors();

    // Getting uniform locations for glUniform* calls

	// Get number of indices from index buffer, which has elements uint16_t
	GLint size = 0;
	glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
	gl_has_errors();

	GLsizei num_indices = size / sizeof(uint16_t);
	// GLsizei num_triangles = num_indices / 3;

    Transform transform;
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    transform.translate(vec2(w,h));
    transform.scale(vec2(6000,-3000));
    mat3 projection = createCameraMatrix();

	GLint color_uloc = glGetUniformLocation(program, "fcolor");
	const vec3 color = vec3(1);
	glUniform3fv(color_uloc, 1, (float *)&color);
	gl_has_errors();
	// Setting uniform values to the currently bound program
	GLuint transform_loc = glGetUniformLocation(program, "transform");
	glUniformMatrix3fv(transform_loc, 1, GL_FALSE, (float *)&transform.mat);
	GLuint projection_loc = glGetUniformLocation(program, "projection");
	glUniformMatrix3fv(projection_loc, 1, GL_FALSE, (float *)&projection);
	gl_has_errors();
	// Drawing of num_indices/3 triangles specified in the index buffer
	glDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, nullptr);
	gl_has_errors();

}

void RenderSystem::drawFloor() {
    const GLuint program = effects[(GLuint)EFFECT_ASSET_ID::TEXTURED];
	glUseProgram(program);
	gl_has_errors();
	// Draw the screen texture on the quad geometry
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[(GLuint)GEOMETRY_BUFFER_ID::FLOOR]);
	glBindBuffer(
		GL_ELEMENT_ARRAY_BUFFER,
		index_buffers[(GLuint)GEOMETRY_BUFFER_ID::FLOOR]); // Note, GL_ELEMENT_ARRAY_BUFFER associates
	// indices to the bound GL_ARRAY_BUFFER
	gl_has_errors();
    GLint in_position_loc = glGetAttribLocation(program, "in_position");
    GLint in_texcoord_loc = glGetAttribLocation(program, "in_texcoord");
    gl_has_errors();
    assert(in_texcoord_loc >= 0);

    glEnableVertexAttribArray(in_position_loc);
    glVertexAttribPointer(in_position_loc, 3, GL_FLOAT, GL_FALSE,
                          sizeof(TexturedVertex), (void *)0);
    gl_has_errors();

    glEnableVertexAttribArray(in_texcoord_loc);
    glVertexAttribPointer(
        in_texcoord_loc, 2, GL_FLOAT, GL_FALSE, sizeof(TexturedVertex),
        (void *)sizeof(
            vec3)); // note the stride to skip the preceeding vertex position

    // Enabling and binding texture to slot 0
    glActiveTexture(GL_TEXTURE0);
    gl_has_errors();

    glBindTexture(GL_TEXTURE_2D, floorTexture);
    gl_has_errors();

    // Getting uniform locations for glUniform* calls

	// Get number of indices from index buffer, which has elements uint16_t
	GLint size = 0;
	glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
	gl_has_errors();

	GLsizei num_indices = size / sizeof(uint16_t);
	// GLsizei num_triangles = num_indices / 3;

    Transform transform;
    /* transform.translate(vec2(500,300)); */
    /* transform.scale(vec2(200,-200)); */
    mat3 projection = createCameraMatrix();

	GLint color_uloc = glGetUniformLocation(program, "fcolor");
	const vec3 color = vec3(1);
	glUniform3fv(color_uloc, 1, (float *)&color);
	gl_has_errors();
	// Setting uniform values to the currently bound program
	GLuint transform_loc = glGetUniformLocation(program, "transform");
	glUniformMatrix3fv(transform_loc, 1, GL_FALSE, (float *)&transform.mat);
	GLuint projection_loc = glGetUniformLocation(program, "projection");
	glUniformMatrix3fv(projection_loc, 1, GL_FALSE, (float *)&projection);
	gl_has_errors();
	// Drawing of num_indices/3 triangles specified in the index buffer
	glDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, nullptr);
	gl_has_errors();

}

void RenderSystem::drawSpaceship() {
    const GLuint program = effects[(GLuint)EFFECT_ASSET_ID::TEXTURED];
	glUseProgram(program);
	gl_has_errors();
	// Draw the screen texture on the quad geometry
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[(GLuint)GEOMETRY_BUFFER_ID::UI_COMPONENT]);
	glBindBuffer(
		GL_ELEMENT_ARRAY_BUFFER,
		index_buffers[(GLuint)GEOMETRY_BUFFER_ID::UI_COMPONENT]); // Note, GL_ELEMENT_ARRAY_BUFFER associates
	// indices to the bound GL_ARRAY_BUFFER
	gl_has_errors();
    GLint in_position_loc = glGetAttribLocation(program, "in_position");
    GLint in_texcoord_loc = glGetAttribLocation(program, "in_texcoord");
    gl_has_errors();
    assert(in_texcoord_loc >= 0);

    glEnableVertexAttribArray(in_position_loc);
    glVertexAttribPointer(in_position_loc, 3, GL_FLOAT, GL_FALSE,
                          sizeof(TexturedVertex), (void *)0);
    gl_has_errors();

    glEnableVertexAttribArray(in_texcoord_loc);
    glVertexAttribPointer(
        in_texcoord_loc, 2, GL_FLOAT, GL_FALSE, sizeof(TexturedVertex),
        (void *)sizeof(
            vec3)); // note the stride to skip the preceeding vertex position

    // Enabling and binding texture to slot 0
    glActiveTexture(GL_TEXTURE0);
    gl_has_errors();

    glBindTexture(GL_TEXTURE_2D, spaceshipTexture);
    gl_has_errors();

    // Getting uniform locations for glUniform* calls

	// Get number of indices from index buffer, which has elements uint16_t
	GLint size = 0;
	glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
	gl_has_errors();

	GLsizei num_indices = size / sizeof(uint16_t);
	// GLsizei num_triangles = num_indices / 3;

    int w, h;
    glfwGetWindowSize(window, &w, &h);
    Transform transform;

    transform.translate(vec2(w,h));
    transform.scale(vec2(w*3.0f,-h*3.0f));
    mat3 projection = createCameraMatrix();

	GLint color_uloc = glGetUniformLocation(program, "fcolor");
	const vec3 color = vec3(1);
	glUniform3fv(color_uloc, 1, (float *)&color);
	gl_has_errors();
	// Setting uniform values to the currently bound program
	GLuint transform_loc = glGetUniformLocation(program, "transform");
	glUniformMatrix3fv(transform_loc, 1, GL_FALSE, (float *)&transform.mat);
	GLuint projection_loc = glGetUniformLocation(program, "projection");
	glUniformMatrix3fv(projection_loc, 1, GL_FALSE, (float *)&projection);
	gl_has_errors();
	// Drawing of num_indices/3 triangles specified in the index buffer
	glDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, nullptr);
	gl_has_errors();

}
void RenderSystem::flipActiveButtions(int activeScreen) {
    for (Entity e : registry.clickables.entities) {
        Clickable& c = registry.clickables.get(e);
        bool shouldBeActive = c.screenTiedTo == activeScreen;
        if (shouldBeActive && c.textureID == (int)TEXTURE_ASSET_ID::PLAY_BUTTON) {
            c.isActive = !saveFileExists;
        }
        else if (shouldBeActive &&c.textureID == (int)TEXTURE_ASSET_ID::CONTINUE_BUTTON) {
            c.isActive = saveFileExists;
        }
        else {
            c.isActive = shouldBeActive;
        }

        if (!c.isActive && registry.renderRequests.has(e)) {
            registry.renderRequests.remove(e);
        }
    }
}

vec2 RenderSystem::calculatePosInCamera(const vec2 &position) {
    mat3 cameraMatrix = createCameraMatrix();
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    vec3 updatedPosition = cameraMatrix * vec3(position.x, position.y, 1.0f);
    // Map to [0,1]
    vec2 standardizedPosition = vec2((updatedPosition.x + 1)/2, (updatedPosition.y + 1)/2);
    return {w * standardizedPosition.x, h -  h * standardizedPosition.y};
}

