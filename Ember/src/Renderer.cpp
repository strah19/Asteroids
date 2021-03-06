#include "Renderer.h"
#include "Logger.h"
#include "RendererCommands.h"
#include "TextureAtlas.h"
#include <gtc/matrix_transform.hpp>
#include <glad/glad.h>

namespace Ember {
	glm::mat4 GetModelMatrix(const glm::vec3& position, const glm::vec2& size);
	glm::mat4 GetRotatedModelMatrix(const glm::vec3& position, const glm::vec2& size, const glm::vec3& rotation_orientation, float degree);

	struct RendererData {
		VertexArray* vertex_array;
		VertexBuffer* vertex_buffer;
		IndexBuffer* index_buffer;
		IndirectDrawBuffer* indirect_draw_buffer;

		Shader* current_shader;
		Shader default_shader;
		ShaderStorageBuffer* ssbo;

		uint32_t index_offset = 0;

		uint32_t texture_slot_index = 0;
		uint32_t textures[MAX_TEXTURE_SLOTS];
		glm::mat4 proj_view = glm::mat4(1.0f);
		Camera* camera;

		uint32_t num_of_vertices_in_batch = 0;

		DrawElementsCommand draw_commands[MAX_DRAW_COMMANDS];
		uint32_t base_vert = 0;
		uint32_t draw_count = 0;
		uint32_t current_draw_command_vertex_size = 0;

		Vertex* vertices_base = nullptr;
		Vertex* vertices_ptr = nullptr;

		uint32_t* index_base = nullptr;
		uint32_t* index_ptr = nullptr;
		uint32_t current_material_id = -1;

		int flags;
	};

	static RendererData renderer_data;

	void Renderer::Init() {
		renderer_data.vertex_buffer = new VertexBuffer(sizeof(Vertex) * MAX_VERTEX_COUNT);
		renderer_data.vertex_array = new VertexArray();

		VertexBufferLayout layout;
		layout.AddToBuffer(VertexBufferElement(3, false, VertexShaderType::Float));
		layout.AddToBuffer(VertexBufferElement(4, false, VertexShaderType::Float));
		layout.AddToBuffer(VertexBufferElement(2, false, VertexShaderType::Float));
		layout.AddToBuffer(VertexBufferElement(2, false, VertexShaderType::Float));

		renderer_data.vertex_buffer->SetLayout(layout);

		renderer_data.vertices_base = new Vertex[MAX_VERTEX_COUNT];
		renderer_data.index_base = new uint32_t[MAX_INDEX_COUNT];

		renderer_data.index_buffer = new IndexBuffer(MAX_INDEX_COUNT * sizeof(uint32_t));
		renderer_data.vertex_array->SetIndexBufferSize(renderer_data.index_buffer->GetCount());
		renderer_data.vertex_array->AddVertexBuffer(renderer_data.vertex_buffer, VertexBufferFormat::VNCVNCVNC);

		renderer_data.indirect_draw_buffer = new IndirectDrawBuffer(sizeof(renderer_data.draw_commands));

		renderer_data.default_shader.Init("shaders/default_shader.glsl");
		InitRendererShader(&renderer_data.default_shader);

		renderer_data.ssbo = new ShaderStorageBuffer(sizeof(glm::mat4), 0);
	}

	void Renderer::Destroy() {
		delete renderer_data.vertex_array;
		delete renderer_data.vertex_buffer;
		delete renderer_data.index_buffer;
		delete renderer_data.indirect_draw_buffer;

		delete[] renderer_data.vertices_base;
		delete[] renderer_data.index_base;
	}

	void Renderer::InitRendererShader(Shader* shader) {
		shader->Bind();
		int sampler[MAX_TEXTURE_SLOTS];
		for (int i = 0; i < MAX_TEXTURE_SLOTS; i++) {
			sampler[i] = i;
		}
		shader->SetIntArray("textures", sampler, MAX_TEXTURE_SLOTS);
	}

	void Renderer::BeginScene(Camera& camera, int flags) {
		renderer_data.flags = flags;
		renderer_data.proj_view = camera.GetProjection() * camera.GetView();

		renderer_data.camera = &camera;
		renderer_data.current_shader = &renderer_data.default_shader;
		renderer_data.current_material_id = -1;
		StartBatch();
	}

	void Renderer::EndScene() {
		MakeCommand();
		GoToNextDrawCommand();
		Render();
	}

	uint32_t Renderer::GetShaderId() {
		return renderer_data.current_shader->GetId();
	}

	void Renderer::StartBatch() {
		memset(renderer_data.textures, NULL, renderer_data.texture_slot_index * sizeof(Texture));
		renderer_data.texture_slot_index = 0;
		renderer_data.num_of_vertices_in_batch = 0;
		renderer_data.index_offset = 0;

		renderer_data.base_vert = 0;
		renderer_data.draw_count = 0;
		renderer_data.current_draw_command_vertex_size = 0;

		renderer_data.vertices_ptr = renderer_data.vertices_base;
		renderer_data.index_ptr = renderer_data.index_base;
	}

	void Renderer::SetPolygonLineThickness(float thickness) {
		if (thickness > 0)
			glLineWidth(thickness);
	}

	void Renderer::Render() {
		if ((renderer_data.flags & RenderFlags::PolygonMode))
			RendererCommand::PolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		renderer_data.vertex_array->Bind();
		renderer_data.index_buffer->Bind();
		renderer_data.vertex_buffer->Bind();

		renderer_data.indirect_draw_buffer->Bind();
		renderer_data.indirect_draw_buffer->SetData(renderer_data.draw_commands, sizeof(renderer_data.draw_commands), 0);

		renderer_data.current_shader->Bind();

		renderer_data.ssbo->Bind();
		renderer_data.ssbo->SetData((void*)&renderer_data.proj_view, sizeof(glm::mat4), 0);
		renderer_data.ssbo->BindToBindPoint();

		for (uint32_t i = 0; i < renderer_data.texture_slot_index; i++)
			if (renderer_data.textures[i]) 
				glBindTextureUnit(i, renderer_data.textures[i]);
		uint32_t vertex_buf_size = (uint32_t)((uint8_t*)renderer_data.vertices_ptr - (uint8_t*)renderer_data.vertices_base);
		uint32_t index_buf_size = (uint32_t)((uint8_t*)renderer_data.index_ptr - (uint8_t*)renderer_data.index_base);

		renderer_data.vertex_buffer->SetData(renderer_data.vertices_base, vertex_buf_size);
		renderer_data.index_buffer->SetData(renderer_data.index_base, index_buf_size);

		renderer_data.vertex_array->SetIndexBufferSize(renderer_data.index_buffer->GetCount());

		RendererCommand::DrawMultiIndirect(nullptr, renderer_data.draw_count + 1, 0);
	}

	void Renderer::NewBatch() {
		Render();
		StartBatch();
	}

	void Renderer::Submit(VertexArray* vertex_array, IndexBuffer* index_buffer, Shader* shader) {
		shader->Bind();
		vertex_array->Bind();
		index_buffer->Bind();

		RendererCommand::DrawVertexArray(vertex_array);
	}

	void Renderer::DrawQuad(const glm::mat4& translation, const glm::vec4& color, float texture_id, const glm::vec2 tex_coords[]) {
		if (renderer_data.num_of_vertices_in_batch + QUAD_VERTEX_COUNT > MAX_VERTEX_COUNT)
			NewBatch();

		CalculateSquareIndices();

		for (size_t i = 0; i < QUAD_VERTEX_COUNT; i++) {    
			Vertex vertex;
			vertex.position = translation * QUAD_POSITIONS[i];
			vertex.color = color;
			vertex.texture_coordinates = tex_coords[i];
			vertex.texture_id = texture_id;
			vertex.material_id = (float)renderer_data.current_material_id;

			*renderer_data.vertices_ptr = vertex;
			renderer_data.vertices_ptr++;

			renderer_data.num_of_vertices_in_batch++;
		}

		renderer_data.current_draw_command_vertex_size += 6;
	}

	void Renderer::CalculateSquareIndices() {
		*renderer_data.index_ptr = renderer_data.index_offset;
		renderer_data.index_ptr++;
		*renderer_data.index_ptr = 1 + renderer_data.index_offset;
		renderer_data.index_ptr++;
		*renderer_data.index_ptr = 2 + renderer_data.index_offset;
		renderer_data.index_ptr++;
		*renderer_data.index_ptr = 2 + renderer_data.index_offset;
		renderer_data.index_ptr++;
		*renderer_data.index_ptr = 3 + renderer_data.index_offset;
		renderer_data.index_ptr++;
		*renderer_data.index_ptr = renderer_data.index_offset;
		renderer_data.index_ptr++;

		renderer_data.index_offset += 4;
	}

	void Renderer::CalculateTriangleIndices() {
		*renderer_data.index_ptr = renderer_data.index_offset;
		renderer_data.index_ptr++;
		*renderer_data.index_ptr = 1 + renderer_data.index_offset;
		renderer_data.index_ptr++;
		*renderer_data.index_ptr = 2 + renderer_data.index_offset;
		renderer_data.index_ptr++;

		renderer_data.index_offset += 3;
	}

	void Renderer::DrawTriangle(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color) {
		if (renderer_data.num_of_vertices_in_batch + QUAD_VERTEX_COUNT > MAX_VERTEX_COUNT)
			NewBatch();

		CalculateTriangleIndices();
		glm::mat4 translation = GetModelMatrix(position, size);

		for (size_t i = 0; i < TRIANGLE_VERTEX_COUNT; i++) {
			Vertex vertex;
			vertex.position = translation * TRIANGLE_POSITIONS[i];
			vertex.color = color;
			vertex.texture_coordinates = { 0, 0 };
			vertex.texture_id = -1.0f;
			vertex.material_id = (float)renderer_data.current_material_id;

			*renderer_data.vertices_ptr = vertex;
			renderer_data.vertices_ptr++;

			renderer_data.num_of_vertices_in_batch++;
		}

		renderer_data.current_draw_command_vertex_size += 3;
	}

	void Renderer::DrawTriangle(const glm::vec3& position, float rotation, const glm::vec3& rotation_orientation, const glm::vec2& size, const glm::vec4& color) {
		glm::mat4 translation = GetModelMatrix(position, size);
		translation = glm::rotate(translation, glm::radians(rotation), rotation_orientation);
		if (renderer_data.num_of_vertices_in_batch + QUAD_VERTEX_COUNT > MAX_VERTEX_COUNT)
			NewBatch();

		CalculateTriangleIndices();

		for (size_t i = 0; i < TRIANGLE_VERTEX_COUNT; i++) {
			Vertex vertex;
			vertex.position = translation * TRIANGLE_POSITIONS[i];
			vertex.color = color;
			vertex.texture_coordinates = { 0, 0 };
			vertex.texture_id = -1.0f;
			vertex.material_id = (float)renderer_data.current_material_id;

			*renderer_data.vertices_ptr = vertex;
			renderer_data.vertices_ptr++;

			renderer_data.num_of_vertices_in_batch++;
		}

		renderer_data.current_draw_command_vertex_size += 3;
	}

	void Renderer::DrawLine(const glm::vec2& p1, const glm::vec2& p2, const glm::vec4& color, float width) {
		float x = p2.x - p1.x;
		float y = p2.y - p1.y;
		glm::mat4 trans = glm::translate(glm::mat4(1.0f), { p1.x + (x / 2), p1.y + (y / 2), 0 });
		glm::mat4 scale = glm::scale(glm::mat4(1.0f), { sqrtf((x * x) + (y * y)), width, 1.0f });
		glm::mat4 rotate = glm::rotate(glm::mat4(1.0f), atan2f(y, x), { 0, 0, 1 });

		glm::mat4 model = trans * rotate * scale;

		DrawQuad(model, color, -1, TEX_COORDS);
	}

	void Renderer::GoToNextDrawCommand() {
		renderer_data.draw_count++;
		renderer_data.base_vert += renderer_data.num_of_vertices_in_batch;
		renderer_data.current_draw_command_vertex_size = 0;
	}

	void Renderer::MakeCommand() {
		renderer_data.draw_commands[renderer_data.draw_count].vertex_count = renderer_data.current_draw_command_vertex_size;
		renderer_data.draw_commands[renderer_data.draw_count].instance_count = 1;
		renderer_data.draw_commands[renderer_data.draw_count].first_index = 0;
		renderer_data.draw_commands[renderer_data.draw_count].base_vertex = renderer_data.base_vert;
		renderer_data.draw_commands[renderer_data.draw_count].base_instance = renderer_data.draw_count;
	}

	void Renderer::SetShader(Shader* shader) {
		renderer_data.current_shader = shader;
	}

	void Renderer::SetShaderToDefualt() {
		renderer_data.current_shader = &renderer_data.default_shader;
	}

	void Renderer::SetMaterialId(uint32_t material_id) {
		renderer_data.current_material_id = material_id;
	}

	glm::mat4 GetModelMatrix(const glm::vec3& position, const glm::vec2& size) {
		return (renderer_data.flags & RenderFlags::TopLeftCornerPos) ?
			glm::translate(glm::mat4(1.0f), { position.x + (size.x / 2), position.y + (size.y / 2), position.z }) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f }) :
			glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
	}

	void Renderer::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color) {
		glm::mat4 model = GetModelMatrix(position, size);

		DrawQuad(model, color, -1.0f, TEX_COORDS);
	}

	void Renderer::DrawQuad(const glm::vec3& position, const glm::vec2& size, Texture* texture, const glm::vec4& color) {
		glm::mat4 model = GetModelMatrix(position, size);
		DrawQuad(model, color, CalculateTextureIndex(texture), TEX_COORDS);
	}

	void Renderer::DrawQuad(const glm::vec3& position, const glm::vec2& size, uint32_t texture, const glm::vec4& color) {
		glm::mat4 model = GetModelMatrix(position, size);
		DrawQuad(model, color, CalculateTextureIndex(texture), TEX_COORDS);
	}

	void Renderer::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec2 tex_coords[], const glm::vec4& color) {
		glm::mat4 model = GetModelMatrix(position, size);
		DrawQuad(model, color, -1.0f, tex_coords);
	}

	void Renderer::DrawQuad(const glm::vec3& position, const glm::vec2& size, Texture* texture, const glm::vec2 tex_coords[], const glm::vec4& color) {
		glm::mat4 model = GetModelMatrix(position, size);
		DrawQuad(model, color, CalculateTextureIndex(texture), tex_coords);
	}

	void Renderer::DrawRotatedQuad(const glm::vec3& position, float rotation, const glm::vec3& rotation_orientation, const glm::vec2& size, const glm::vec4& color) {
		glm::mat4 trans = glm::translate(glm::mat4(1.0f), { position.x, position.y, 0 });
		glm::mat4 scale = glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
		glm::mat4 rotate = glm::rotate(glm::mat4(1.0f), glm::radians(rotation), rotation_orientation);

		glm::mat4 model = trans * rotate * scale;

		DrawQuad(model, color, -1.0f, TEX_COORDS);
	}

	void Renderer::DrawRotatedQuad(const glm::vec3& position, float rotation, const glm::vec3& rotation_orientation, const glm::vec2& size, Texture* texture, const glm::vec4& color) {
		glm::mat4 model = GetModelMatrix(position, size);
		model = glm::rotate(model, glm::radians(rotation), rotation_orientation);
		DrawQuad(model, color, CalculateTextureIndex(texture), TEX_COORDS);
	}

	void Renderer::DrawRotatedQuad(const glm::vec3& position, float rotation, const glm::vec3& rotation_orientation, const glm::vec2& size, const glm::vec2 tex_coords[], const glm::vec4& color) {
		glm::mat4 model = GetModelMatrix(position, size);
		model = glm::rotate(model, glm::radians(rotation), rotation_orientation);
		DrawQuad(model, color, -1.0f, tex_coords);
	}

	void Renderer::DrawRotatedQuad(const glm::vec3& position, float rotation, const glm::vec3& rotation_orientation, const glm::vec2& size, const glm::vec2 tex_coords[], Texture* texture, const glm::vec4& color) {
		glm::mat4 model = GetModelMatrix(position, size);
		model = glm::rotate(model, glm::radians(rotation), rotation_orientation);
		DrawQuad(model, color, CalculateTextureIndex(texture), tex_coords);
	}

	void Renderer::DrawCube(const glm::vec3& position, const glm::vec3& size, const glm::vec4& color) {
		glm::mat4 model = glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) * glm::scale(glm::mat4(1.0f), { size.x, size.y, size.z });
		DrawCube(model, color, -1.0f, TEX_COORDS);
	}

	void Renderer::DrawCube(const glm::vec3& position, const glm::vec3& size, Texture* texture, const glm::vec4& color) {
		glm::mat4 model = glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) * glm::scale(glm::mat4(1.0f), { size.x, size.y, size.z });
		DrawCube(model, color, CalculateTextureIndex(texture), TEX_COORDS);
	}

	void Renderer::DrawCube(const glm::mat4& translation, const glm::vec4& color, float texture_id, const glm::vec2 tex_coords[]) {
		if (renderer_data.num_of_vertices_in_batch + CUBE_VERTEX_COUNT > MAX_VERTEX_COUNT)
			NewBatch();

		for (uint32_t i = 0; i < CUBE_FACES; i++)
			CalculateSquareIndices();

		Vertex* start_base = renderer_data.vertices_ptr;
		uint32_t face = -4;
		for (size_t i = 0; i < CUBE_VERTEX_COUNT; i++) {
			if (i % 4 == 0) {
				face += 4;
				Vertex* face_base_ptr = &start_base[face];
			}

			Vertex vertex;
			vertex.position = translation * glm::vec4(CUBE_POSITIONS[i].x, CUBE_POSITIONS[i].y, CUBE_POSITIONS[i].z, 1.0f);
			vertex.color = color;
			vertex.texture_coordinates = tex_coords[i % 4];
			vertex.texture_id = texture_id;
			vertex.material_id = (float)renderer_data.current_material_id;

			*renderer_data.vertices_ptr = vertex;
			renderer_data.vertices_ptr++;

			renderer_data.num_of_vertices_in_batch++;
		}

		renderer_data.current_draw_command_vertex_size += 36;
	}

	void Renderer::RenderText(Font* font, const std::string& text, const glm::vec2& pos, const glm::vec2& scale, const glm::vec4& color) {
		float x = pos.x;
		float y= pos.y;

		for (auto& c : text) {
			Glyph character = font->glyphs[c];
			float normalized_width = TextureAtlas::CalculateSpriteCoordinate({ character.size.x, 0 }, font->width, font->height).x - 0.00002f * font->size;

			float xpos = x + character.bearing.x * scale.x;
			float ypos = y - (character.size.y - character.bearing.y) * scale.y;

			float w = character.size.x * scale.x;
			float h = character.size.y * scale.y;

			float clean = 0.00001f * font->size;

			glm::vec2 coords[] = {
				{ character.offset + clean, 1.0f },
				{ character.offset + normalized_width + clean, 1.0f },
				{ character.offset + normalized_width + clean, 0.0f },
				{ character.offset + clean, 0.0f }
			};

			if (renderer_data.num_of_vertices_in_batch + QUAD_VERTEX_COUNT > MAX_VERTEX_COUNT)
				NewBatch();

			CalculateSquareIndices();
			float tex_id = CalculateTextureIndex(font->texture);

			glm::vec4 pos[] = {
				{ xpos, ypos, 0.0f, 1.0f },
				{ xpos + w,  ypos, 0.0f, 1.0f },
				{ xpos + w,  ypos + h, 0.0f, 1.0f },
				{ xpos, ypos + h, 0.0f, 1.0f },

			};

			for (size_t i = 0; i < QUAD_VERTEX_COUNT; i++) {
				Vertex vertex;
				vertex.position = pos[i];
				vertex.color = color;
				vertex.texture_coordinates = coords[i];
				vertex.texture_id = tex_id;
				vertex.material_id = (float)renderer_data.current_material_id;

				*renderer_data.vertices_ptr = vertex;
				renderer_data.vertices_ptr++;
				 
				renderer_data.num_of_vertices_in_batch++;
			}

			renderer_data.current_draw_command_vertex_size += 6;


			x += (character.advance.x >> 6) * scale.x;
		}
	}

	float Renderer::CalculateTextureIndex(Texture* texture) {
		return CalculateTextureIndex(texture->GetTextureId());
	}

	float Renderer::CalculateTextureIndex(uint32_t id) {
		float texture_id = -1.0f;

		for (uint32_t i = 0; i < renderer_data.texture_slot_index; i++)
			if (renderer_data.textures[i] == id)
				texture_id = (float)i;

		if (texture_id == -1.0f) {
			renderer_data.textures[renderer_data.texture_slot_index] = id;
			texture_id = (float)renderer_data.texture_slot_index;
			renderer_data.texture_slot_index++;

			if (renderer_data.texture_slot_index == MAX_TEXTURE_SLOTS)
				NewBatch();
		}

		return texture_id;
	}
}