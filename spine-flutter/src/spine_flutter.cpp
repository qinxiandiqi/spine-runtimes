/******************************************************************************
 * Spine Runtimes License Agreement
 * Last updated September 24, 2021. Replaces all prior versions.
 *
 * Copyright (c) 2013-2021, Esoteric Software LLC
 *
 * Integration of the Spine Runtimes into software or otherwise creating
 * derivative works of the Spine Runtimes is permitted under the terms and
 * conditions of Section 2 of the Spine Editor License Agreement:
 * http://esotericsoftware.com/spine-editor-license
 *
 * Otherwise, it is permitted to integrate the Spine Runtimes into software
 * or otherwise create derivative works of the Spine Runtimes (collectively,
 * "Products"), provided that each user of the Products must obtain their own
 * Spine Editor license and redistribution of the Products in any form must
 * include this license and copyright notice.
 *
 * THE SPINE RUNTIMES ARE PROVIDED BY ESOTERIC SOFTWARE LLC "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ESOTERIC SOFTWARE LLC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES,
 * BUSINESS INTERRUPTION, OR LOSS OF USE, DATA, OR PROFITS) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THE SPINE RUNTIMES, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include "spine_flutter.h"
#include <spine/spine.h>
#include <spine/Version.h>
#include <spine/Debug.h>

using namespace spine;

struct Block {
	int size;
	int allocated;
	uint8_t* memory;

	int free() {
		return size - allocated;
	}

	bool canFit(int numBytes) {
		return free() >= numBytes;
	}

	uint8_t* allocate(int numBytes) {
		uint8_t *ptr = memory + allocated;
		memset(ptr, 0, numBytes);
		allocated += numBytes;
		return ptr;
	}
};

class BlockAllocator : public SpineObject{
	int initialBlockSize;
	Vector<Block> blocks;

public:
	BlockAllocator(int initialBlockSize) : initialBlockSize(initialBlockSize) {
		blocks.add(newBlock(initialBlockSize));
	}

	~BlockAllocator() {
        for (int i = 0; i < blocks.size(); i++) {
            SpineExtension::free(blocks[i].memory, __FILE__, __LINE__);
        }
	}

	Block newBlock(int numBytes) {
		Block block = {MathUtil::max(initialBlockSize, numBytes), 0, nullptr};
		block.memory = SpineExtension::alloc<uint8_t>(block.size, __FILE__, __LINE__);
		return block;
	}

	template<typename T>
	T *allocate(size_t num) {
		return (T *) _allocate((int)(sizeof(T) * num));
	}

	void compress() {
		int totalSize = 0;
		for (int i = 0; i < blocks.size(); i++) {
			totalSize += blocks[i].size;
			SpineExtension::free(blocks[i].memory, __FILE__, __LINE__);
		}
		blocks.clear();
		blocks.add(newBlock(totalSize));
	}
private:
    void *_allocate(int numBytes) {
        // 16-byte align allocations
        int alignedNumBytes = numBytes + (numBytes % 16 != 0 ? 16 - (numBytes % 16) : 0);
        Block *block = &blocks[blocks.size() - 1];
        if (!block->canFit(alignedNumBytes)) {
            blocks.add(newBlock(MathUtil::max(initialBlockSize, alignedNumBytes)));
            block = &blocks[blocks.size() - 1];
        }
        return block->allocate(alignedNumBytes);
    }
};

struct AnimationStateEvent {
	EventType type;
	TrackEntry *entry;
	Event *event;
	AnimationStateEvent(EventType type, TrackEntry *entry, Event *event) : type(type), entry(entry), event(event){};
};

struct EventListener : public AnimationStateListenerObject {
	Vector<AnimationStateEvent> events;

	void callback(AnimationState *state, EventType type, TrackEntry *entry, Event *event) {
		events.add(AnimationStateEvent(type, entry, event));
	}
};

typedef struct _spine_atlas {
	void *atlas;
	utf8 **imagePaths;
	int32_t numImagePaths;
	utf8 *error;
} _spine_atlas;

typedef struct _spine_skeleton_data_result {
	spine_skeleton_data skeletonData;
	utf8 *error;
} _spine_skeleton_data_result;

typedef struct _spine_render_command {
	float *positions;
	float *uvs;
	int32_t *colors;
	int32_t numVertices;
	uint16_t *indices;
	int32_t numIndices;
	int32_t atlasPage;
	spine_blend_mode blendMode;
	struct _spine_render_command *next;
} _spine_render_command;

typedef struct _spine_bounds {
	float x, y, width, height;
} _spine_bounds;

typedef struct _spine_vector {
	float x, y;
} _spine_vector;

typedef struct _spine_skeleton_drawable {
	spine_skeleton skeleton;
	spine_animation_state animationState;
	spine_animation_state_data animationStateData;
	spine_animation_state_events animationStateEvents;
	void *clipping;
	_spine_render_command *renderCommand;
	BlockAllocator *allocator;
} _spine_skeleton_drawable;

typedef struct _spine_skin_entry {
	int32_t slotIndex;
	utf8 *name;
	spine_attachment attachment;
} _spine_skin_entry;

typedef struct _spine_skin_entries {
	int32_t numEntries;
	_spine_skin_entry *entries;
} _spine_skin_entries;

static Color NULL_COLOR(0, 0, 0, 0);

static SpineExtension *defaultExtension = nullptr;
static DebugExtension *debugExtension = nullptr;

static void initExtensions() {
    if (defaultExtension == nullptr) {
        defaultExtension = new DefaultSpineExtension();
        debugExtension = new DebugExtension(defaultExtension);
    }
}

spine::SpineExtension *spine::getDefaultExtension() {
    initExtensions();
	return defaultExtension;
}

void spine_enable_debug_extension(int enable) {
    initExtensions();
	SpineExtension::setInstance(enable ? debugExtension : defaultExtension);
}

int32_t spine_major_version() {
	return SPINE_MAJOR_VERSION;
}

int32_t spine_minor_version() {
	return SPINE_MINOR_VERSION;
}

void spine_report_leaks() {
    initExtensions();
	debugExtension->reportLeaks();
	fflush(stdout);
}

// Color

float spine_color_get_r(spine_color color) {
	if (!color) return 0;
	return ((Color *) color)->r;
}

float spine_color_get_g(spine_color color) {
	if (!color) return 0;
	return ((Color *) color)->g;
}

float spine_color_get_b(spine_color color) {
	if (!color) return 0;
	return ((Color *) color)->b;
}

float spine_color_get_a(spine_color color) {
	if (!color) return 0;
	return ((Color *) color)->a;
}

// Bounds

float spine_bounds_get_x(spine_bounds bounds) {
	if (!bounds) return 0;
	return ((_spine_bounds *) bounds)->x;
}

float spine_bounds_get_y(spine_bounds bounds) {
	if (!bounds) return 0;
	return ((_spine_bounds *) bounds)->y;
}

float spine_bounds_get_width(spine_bounds bounds) {
	if (!bounds) return 0;
	return ((_spine_bounds *) bounds)->width;
}

float spine_bounds_get_height(spine_bounds bounds) {
	if (!bounds) return 0;
	return ((_spine_bounds *) bounds)->height;
}

// Vector
float spine_vector_get_x(spine_vector vector) {
	if (!vector) return 0;
	return ((_spine_vector *) vector)->x;
}

float spine_vector_get_y(spine_vector vector) {
	if (!vector) return 0;
	return ((_spine_vector *) vector)->y;
}

// Atlas

spine_atlas spine_atlas_load(const utf8 *atlasData) {
	if (!atlasData) return nullptr;
	int32_t length = (int32_t) strlen(atlasData);
	auto atlas = new (__FILE__, __LINE__) Atlas(atlasData, length, "", (TextureLoader *) nullptr, false);
	_spine_atlas *result = SpineExtension::calloc<_spine_atlas>(1, __FILE__, __LINE__);
	result->atlas = atlas;
	result->numImagePaths = (int32_t) atlas->getPages().size();
	result->imagePaths = SpineExtension::calloc<utf8 *>(result->numImagePaths, __FILE__, __LINE__);
	for (int i = 0; i < result->numImagePaths; i++) {
		result->imagePaths[i] = (utf8 *) strdup(atlas->getPages()[i]->texturePath.buffer());
	}
	return (spine_atlas) result;
}

int32_t spine_atlas_get_num_image_paths(spine_atlas atlas) {
	if (!atlas) return 0;
	return ((_spine_atlas *) atlas)->numImagePaths;
}

utf8 *spine_atlas_get_image_path(spine_atlas atlas, int32_t index) {
	if (!atlas) return nullptr;
	return ((_spine_atlas *) atlas)->imagePaths[index];
}

utf8 *spine_atlas_get_error(spine_atlas atlas) {
	if (!atlas) return nullptr;
	return ((_spine_atlas *) atlas)->error;
}

void spine_atlas_dispose(spine_atlas atlas) {
	if (!atlas) return;
	_spine_atlas *_atlas = (_spine_atlas *) atlas;
	if (_atlas->atlas) delete (Atlas *) _atlas->atlas;
	if (_atlas->error) free(_atlas->error);
	for (int i = 0; i < _atlas->numImagePaths; i++) {
		free(_atlas->imagePaths[i]);
	}
	SpineExtension::free(_atlas->imagePaths, __FILE__, __LINE__);
	SpineExtension::free(_atlas, __FILE__, __LINE__);
}

// SkeletonData

spine_skeleton_data_result spine_skeleton_data_load_json(spine_atlas atlas, const utf8 *skeletonData) {
	_spine_skeleton_data_result *result = SpineExtension::calloc<_spine_skeleton_data_result>(1, __FILE__, __LINE__);
	_spine_atlas *_atlas = (_spine_atlas *) atlas;
	Bone::setYDown(true);
	if (!_atlas) return (spine_skeleton_data_result) result;
	if (!_atlas->atlas) return (spine_skeleton_data_result) result;
	if (!skeletonData) return (spine_skeleton_data_result) result;
	SkeletonJson json((Atlas *) _atlas->atlas);
	SkeletonData *data = json.readSkeletonData(skeletonData);
	result->skeletonData = (spine_skeleton_data) data;
	if (!json.getError().isEmpty()) {
		result->error = (utf8 *) strdup(json.getError().buffer());
	}
	return (spine_skeleton_data_result) result;
}

spine_skeleton_data_result spine_skeleton_data_load_binary(spine_atlas atlas, const uint8_t *skeletonData, int32_t length) {
	_spine_skeleton_data_result *result = SpineExtension::calloc<_spine_skeleton_data_result>(1, __FILE__, __LINE__);
	_spine_atlas *_atlas = (_spine_atlas *) atlas;
	Bone::setYDown(true);
	if (!_atlas) return (spine_skeleton_data_result) result;
	if (!_atlas->atlas) return (spine_skeleton_data_result) result;
	if (!skeletonData) return (spine_skeleton_data_result) result;
	if (length <= 0) return (spine_skeleton_data_result) result;
	SkeletonBinary binary((Atlas *) _atlas->atlas);
	SkeletonData *data = binary.readSkeletonData(skeletonData, length);
	result->skeletonData = (spine_skeleton_data) data;
	if (!binary.getError().isEmpty()) {
		result->error = (utf8 *) strdup(binary.getError().buffer());
	}
	return (spine_skeleton_data_result) result;
}

utf8 *spine_skeleton_data_result_get_error(spine_skeleton_data_result result) {
	if (!result) return nullptr;
	return ((_spine_skeleton_data_result *) result)->error;
}
spine_skeleton_data spine_skeleton_data_result_get_data(spine_skeleton_data_result result) {
	if (!result) return nullptr;
	return ((_spine_skeleton_data_result *) result)->skeletonData;
}

void spine_skeleton_data_result_dispose(spine_skeleton_data_result result) {
	if (!result) return;
	_spine_skeleton_data_result *_result = (_spine_skeleton_data_result *) result;
	if (_result->error) SpineExtension::free(_result->error, __FILE__, __LINE__);
	SpineExtension::free(_result, __FILE__, __LINE__);
}

spine_bone_data spine_skeleton_data_find_bone(spine_skeleton_data data, const utf8 *name) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_bone_data) _data->findBone(name);
}

spine_slot_data spine_skeleton_data_find_slot(spine_skeleton_data data, const utf8 *name) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_slot_data) _data->findSlot(name);
}

spine_skin spine_skeleton_data_find_skin(spine_skeleton_data data, const utf8 *name) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_skin) _data->findSkin(name);
}

spine_event_data spine_skeleton_data_find_event(spine_skeleton_data data, const utf8 *name) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_event_data) _data->findEvent(name);
}

spine_animation spine_skeleton_data_find_animation(spine_skeleton_data data, const utf8 *name) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_animation) _data->findAnimation(name);
}

spine_ik_constraint_data spine_skeleton_data_find_ik_constraint(spine_skeleton_data data, const utf8 *name) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_ik_constraint_data) _data->findIkConstraint(name);
}

spine_transform_constraint_data spine_skeleton_data_find_transform_constraint(spine_skeleton_data data, const utf8 *name) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_transform_constraint_data) _data->findTransformConstraint(name);
}

spine_path_constraint_data spine_skeleton_data_find_path_constraint(spine_skeleton_data data, const utf8 *name) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_path_constraint_data) _data->findPathConstraint(name);
}

const utf8 *spine_skeleton_data_get_name(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (utf8 *) _data->getName().buffer();
}

int32_t spine_skeleton_data_get_num_bones(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return (int32_t) _data->getBones().size();
}

spine_bone_data *spine_skeleton_data_get_bones(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_bone_data *) _data->getBones().buffer();
}

int32_t spine_skeleton_data_get_num_slots(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return (int32_t) _data->getSlots().size();
}

spine_slot_data *spine_skeleton_data_get_slots(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_slot_data *) _data->getSlots().buffer();
}

int32_t spine_skeleton_data_get_num_skins(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return (int32_t) _data->getSkins().size();
}

spine_skin *spine_skeleton_data_get_skins(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_skin *) _data->getSkins().buffer();
}

spine_skin spine_skeleton_data_get_default_skin(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_skin) _data->getDefaultSkin();
}

void spine_skeleton_data_set_default_skin(spine_skeleton_data data, spine_skin skin) {
	if (data == nullptr) return;
	SkeletonData *_data = (SkeletonData *) data;
	_data->setDefaultSkin((Skin *) skin);
}

int32_t spine_skeleton_data_get_num_events(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return (int32_t) _data->getEvents().size();
}

spine_event_data *spine_skeleton_data_get_events(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_event_data *) _data->getEvents().buffer();
}

int32_t spine_skeleton_data_get_num_animations(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return (int32_t) _data->getAnimations().size();
}

spine_animation *spine_skeleton_data_get_animations(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_animation *) _data->getAnimations().buffer();
}

int32_t spine_skeleton_data_get_num_ik_constraints(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return (int32_t) _data->getIkConstraints().size();
}

spine_ik_constraint_data *spine_skeleton_data_get_ik_constraints(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_ik_constraint_data *) _data->getIkConstraints().buffer();
}

int32_t spine_skeleton_data_get_num_transform_constraints(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return (int32_t) _data->getTransformConstraints().size();
}

spine_transform_constraint_data *spine_skeleton_data_get_transform_constraints(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_transform_constraint_data *) _data->getTransformConstraints().buffer();
}

int32_t spine_skeleton_data_get_num_path_constraints(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return (int32_t) _data->getPathConstraints().size();
}

spine_path_constraint_data *spine_skeleton_data_get_path_constraints(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (spine_path_constraint_data *) _data->getPathConstraints().buffer();
}

float spine_skeleton_data_get_x(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return _data->getX();
}

void spine_skeleton_data_set_x(spine_skeleton_data data, float x) {
	if (data == nullptr) return;
	SkeletonData *_data = (SkeletonData *) data;
	_data->setX(x);
}

float spine_skeleton_data_get_y(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return _data->getY();
}

void spine_skeleton_data_set_y(spine_skeleton_data data, float y) {
	if (data == nullptr) return;
	SkeletonData *_data = (SkeletonData *) data;
	_data->setY(y);
}

float spine_skeleton_data_get_width(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return _data->getWidth();
}

void spine_skeleton_data_set_width(spine_skeleton_data data, float width) {
	if (data == nullptr) return;
	SkeletonData *_data = (SkeletonData *) data;
	_data->setWidth(width);
}

float spine_skeleton_data_get_height(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return _data->getHeight();
}

void spine_skeleton_data_set_height(spine_skeleton_data data, float height) {
	if (data == nullptr) return;
	SkeletonData *_data = (SkeletonData *) data;
	_data->setHeight(height);
}

const utf8 *spine_skeleton_data_get_version(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (utf8 *) _data->getVersion().buffer();
}

const utf8 *spine_skeleton_data_get_hash(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (utf8 *) _data->getHash().buffer();
}

const utf8 *spine_skeleton_data_get_images_path(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (utf8 *) _data->getImagesPath().buffer();
}

const utf8 *spine_skeleton_data_get_audio_path(spine_skeleton_data data) {
	if (data == nullptr) return nullptr;
	SkeletonData *_data = (SkeletonData *) data;
	return (utf8 *) _data->getAudioPath().buffer();
}

float spine_skeleton_data_get_fps(spine_skeleton_data data) {
	if (data == nullptr) return 0;
	SkeletonData *_data = (SkeletonData *) data;
	return _data->getFps();
}

void spine_skeleton_data_dispose(spine_skeleton_data data) {
	if (!data) return;
	delete (SkeletonData *) data;
}

// RenderCommand
static _spine_render_command *spine_render_command_create(BlockAllocator &allocator, int numVertices, int32_t numIndices, spine_blend_mode blendMode, int32_t pageIndex) {
	_spine_render_command *cmd = allocator.allocate<_spine_render_command>(1);
	cmd->positions = allocator.allocate<float>(numVertices << 1);
	cmd->uvs = allocator.allocate<float>(numVertices << 1);
	cmd->colors = allocator.allocate<int32_t>(numVertices);
	cmd->numVertices = numVertices;
	cmd->indices = allocator.allocate<uint16_t>(numIndices);
	cmd->numIndices = numIndices;
	cmd->blendMode = blendMode;
	cmd->atlasPage = pageIndex;
	cmd->next = nullptr;
	return cmd;
}

// SkeletonDrawable

spine_skeleton_drawable spine_skeleton_drawable_create(spine_skeleton_data skeletonData) {
	_spine_skeleton_drawable *drawable = SpineExtension::calloc<_spine_skeleton_drawable>(1, __FILE__, __LINE__);
	drawable->skeleton = (spine_skeleton) new (__FILE__, __LINE__) Skeleton((SkeletonData *) skeletonData);
	AnimationStateData *stateData = new (__FILE__, __LINE__) AnimationStateData((SkeletonData *) skeletonData);
	drawable->animationStateData = (spine_animation_state_data) stateData;
	AnimationState *state = new (__FILE__, __LINE__) AnimationState(stateData);
	drawable->animationState = (spine_animation_state) state;
	state->setManualTrackEntryDisposal(true);
	EventListener *listener = new EventListener();
	drawable->animationStateEvents = (spine_animation_state_events) listener;
	state->setListener(listener);
	drawable->clipping = new (__FILE__, __LINE__) SkeletonClipping();
	drawable->allocator = new (__FILE__, __LINE__) BlockAllocator(2048);
	return (spine_skeleton_drawable) drawable;
}

void spine_skeleton_drawable_dispose(spine_skeleton_drawable drawable) {
	_spine_skeleton_drawable *_drawable = (_spine_skeleton_drawable *) drawable;
	if (!_drawable) return;
	if (_drawable->skeleton) delete (Skeleton *) _drawable->skeleton;
	if (_drawable->animationState) delete (AnimationState *) _drawable->animationState;
	if (_drawable->animationStateData) delete (AnimationStateData *) _drawable->animationStateData;
	if (_drawable->animationStateEvents) delete (Vector<AnimationStateEvent> *) (_drawable->animationStateEvents);
	if (_drawable->clipping) delete (SkeletonClipping *) _drawable->clipping;
	if (_drawable->allocator) delete (BlockAllocator *) _drawable->allocator;
	SpineExtension::free(drawable, __FILE__, __LINE__);
}

spine_render_command spine_skeleton_drawable_render(spine_skeleton_drawable drawable) {
	_spine_skeleton_drawable *_drawable = (_spine_skeleton_drawable *) drawable;
	if (!_drawable) return nullptr;
	if (!_drawable->skeleton) return nullptr;

	_drawable->allocator->compress();
	_drawable->renderCommand = nullptr;

	Vector<unsigned short> quadIndices;
	quadIndices.add(0);
	quadIndices.add(1);
	quadIndices.add(2);
	quadIndices.add(2);
	quadIndices.add(3);
	quadIndices.add(0);
	Vector<float> worldVertices;
	SkeletonClipping &clipper = *(SkeletonClipping *) _drawable->clipping;
	Skeleton *skeleton = (Skeleton *) _drawable->skeleton;
	_spine_render_command *lastCommand = nullptr;

	for (unsigned i = 0; i < skeleton->getSlots().size(); ++i) {
		Slot &slot = *skeleton->getDrawOrder()[i];
		Attachment *attachment = slot.getAttachment();
		if (!attachment) continue;

		// Early out if the slot color is 0 or the bone is not active
		if (slot.getColor().a == 0 || !slot.getBone().isActive()) {
			clipper.clipEnd(slot);
			continue;
		}

		Vector<float> *vertices = &worldVertices;
		int32_t verticesCount;
		Vector<float> *uvs;
		Vector<unsigned short> *indices;
		int32_t indicesCount;
		Color *attachmentColor;
		int32_t pageIndex;

		if (attachment->getRTTI().isExactly(RegionAttachment::rtti)) {
			RegionAttachment *regionAttachment = (RegionAttachment *) attachment;
			attachmentColor = &regionAttachment->getColor();

			// Early out if the slot color is 0
			if (attachmentColor->a == 0) {
				clipper.clipEnd(slot);
				continue;
			}

			worldVertices.setSize(8, 0);
			regionAttachment->computeWorldVertices(slot, worldVertices, 0, 2);
			verticesCount = 4;
			uvs = &regionAttachment->getUVs();
			indices = &quadIndices;
			indicesCount = 6;
			pageIndex = ((AtlasRegion *) regionAttachment->getRegion())->page->index;

		} else if (attachment->getRTTI().isExactly(MeshAttachment::rtti)) {
			MeshAttachment *mesh = (MeshAttachment *) attachment;
			attachmentColor = &mesh->getColor();

			// Early out if the slot color is 0
			if (attachmentColor->a == 0) {
				clipper.clipEnd(slot);
				continue;
			}

			worldVertices.setSize(mesh->getWorldVerticesLength(), 0);
			mesh->computeWorldVertices(slot, 0, mesh->getWorldVerticesLength(), worldVertices.buffer(), 0, 2);
			verticesCount = (int32_t) (mesh->getWorldVerticesLength() >> 1);
			uvs = &mesh->getUVs();
			indices = &mesh->getTriangles();
			indicesCount = (int32_t) indices->size();
			pageIndex = ((AtlasRegion *) mesh->getRegion())->page->index;

		} else if (attachment->getRTTI().isExactly(ClippingAttachment::rtti)) {
			ClippingAttachment *clip = (ClippingAttachment *) slot.getAttachment();
			clipper.clipStart(slot, clip);
			continue;
		} else
			continue;

		uint8_t r = static_cast<uint8_t>(skeleton->getColor().r * slot.getColor().r * attachmentColor->r * 255);
		uint8_t g = static_cast<uint8_t>(skeleton->getColor().g * slot.getColor().g * attachmentColor->g * 255);
		uint8_t b = static_cast<uint8_t>(skeleton->getColor().b * slot.getColor().b * attachmentColor->b * 255);
		uint8_t a = static_cast<uint8_t>(skeleton->getColor().a * slot.getColor().a * attachmentColor->a * 255);
		uint32_t color = (a << 24) | (r << 16) | (g << 8) | b;

		if (clipper.isClipping()) {
			clipper.clipTriangles(worldVertices, *indices, *uvs, 2);
			vertices = &clipper.getClippedVertices();
			verticesCount = (int32_t) (clipper.getClippedVertices().size() >> 1);
			uvs = &clipper.getClippedUVs();
			indices = &clipper.getClippedTriangles();
			indicesCount = (int32_t) (clipper.getClippedTriangles().size());
		}

		_spine_render_command *cmd = spine_render_command_create(*_drawable->allocator, verticesCount, indicesCount, (spine_blend_mode) slot.getData().getBlendMode(), pageIndex);

		memcpy(cmd->positions, vertices->buffer(), (verticesCount << 1) * sizeof(float));
		memcpy(cmd->uvs, uvs->buffer(), (verticesCount << 1) * sizeof(float));
		for (int ii = 0; ii < verticesCount; ii++) cmd->colors[ii] = color;
		memcpy(cmd->indices, indices->buffer(), indices->size() * sizeof(uint16_t));

		if (!lastCommand) {
			_drawable->renderCommand = lastCommand = cmd;
		} else {
			lastCommand->next = cmd;
			lastCommand = cmd;
		}

		clipper.clipEnd(slot);
	}
	clipper.clipEnd();

	return (spine_render_command) _drawable->renderCommand;
}

spine_skeleton spine_skeleton_drawable_get_skeleton(spine_skeleton_drawable drawable) {
	if (!drawable) return nullptr;
	return ((_spine_skeleton_drawable *) drawable)->skeleton;
}

spine_animation_state spine_skeleton_drawable_get_animation_state(spine_skeleton_drawable drawable) {
	if (!drawable) return nullptr;
	return ((_spine_skeleton_drawable *) drawable)->animationState;
}

spine_animation_state_data spine_skeleton_drawable_get_animation_state_data(spine_skeleton_drawable drawable) {
	if (!drawable) return nullptr;
	return ((_spine_skeleton_drawable *) drawable)->animationStateData;
}

spine_animation_state_events spine_skeleton_drawable_get_animation_state_events(spine_skeleton_drawable drawable) {
	if (!drawable) return nullptr;
	return ((_spine_skeleton_drawable *) drawable)->animationStateEvents;
}

// Render command
float *spine_render_command_get_positions(spine_render_command command) {
	if (!command) return nullptr;
	return ((_spine_render_command *) command)->positions;
}

float *spine_render_command_get_uvs(spine_render_command command) {
	if (!command) return nullptr;
	return ((_spine_render_command *) command)->uvs;
}

int32_t *spine_render_command_get_colors(spine_render_command command) {
	if (!command) return nullptr;
	return ((_spine_render_command *) command)->colors;
}

int32_t spine_render_command_get_num_vertices(spine_render_command command) {
	if (!command) return 0;
	return ((_spine_render_command *) command)->numVertices;
}

uint16_t *spine_render_command_get_indices(spine_render_command command) {
	if (!command) return nullptr;
	return ((_spine_render_command *) command)->indices;
}

int32_t spine_render_command_get_num_indices(spine_render_command command) {
	if (!command) return 0;
	return ((_spine_render_command *) command)->numIndices;
}

int32_t spine_render_command_get_atlas_page(spine_render_command command) {
	if (!command) return 0;
	return ((_spine_render_command *) command)->atlasPage;
}

spine_blend_mode spine_render_command_get_blend_mode(spine_render_command command) {
	if (!command) return SPINE_BLEND_MODE_NORMAL;
	return ((_spine_render_command *) command)->blendMode;
}

spine_render_command spine_render_command_get_next(spine_render_command command) {
	if (!command) return nullptr;
	return (spine_render_command) ((_spine_render_command *) command)->next;
}

// Animation

const utf8 *spine_animation_get_name(spine_animation animation) {
	if (animation == nullptr) return nullptr;
	Animation *_animation = (Animation *) animation;
	return (utf8 *) _animation->getName().buffer();
}

float spine_animation_get_duration(spine_animation animation) {
	if (animation == nullptr) return 0;
	Animation *_animation = (Animation *) animation;
	return _animation->getDuration();
}

// AnimationStateData
spine_skeleton_data spine_animation_state_data_get_skeleton_data(spine_animation_state_data stateData) {
	if (stateData == nullptr) return nullptr;
	AnimationStateData *_stateData = (AnimationStateData *) stateData;
	return (spine_skeleton_data) _stateData->getSkeletonData();
}

float spine_animation_state_data_get_default_mix(spine_animation_state_data stateData) {
	if (stateData == nullptr) return 0;
	AnimationStateData *_stateData = (AnimationStateData *) stateData;
	return _stateData->getDefaultMix();
}

void spine_animation_state_data_set_default_mix(spine_animation_state_data stateData, float defaultMix) {
	if (stateData == nullptr) return;
	AnimationStateData *_stateData = (AnimationStateData *) stateData;
	_stateData->setDefaultMix(defaultMix);
}

void spine_animation_state_data_set_mix(spine_animation_state_data stateData, spine_animation from, spine_animation to, float duration) {
	if (stateData == nullptr) return;
	if (from == nullptr || to == nullptr) return;
	AnimationStateData *_stateData = (AnimationStateData *) stateData;
	_stateData->setMix((Animation *) from, (Animation *) to, duration);
}

float spine_animation_state_data_get_mix(spine_animation_state_data stateData, spine_animation from, spine_animation to) {
	if (stateData == nullptr) return 0;
	if (from == nullptr || to == nullptr) return 0;
	AnimationStateData *_stateData = (AnimationStateData *) stateData;
	return _stateData->getMix((Animation *) from, (Animation *) to);
}

void spine_animation_state_data_set_mix_by_name(spine_animation_state_data stateData, const utf8 *fromName, const utf8 *toName, float duration) {
	if (stateData == nullptr) return;
	if (fromName == nullptr || toName == nullptr) return;
	AnimationStateData *_stateData = (AnimationStateData *) stateData;
	_stateData->setMix(fromName, toName, duration);
}

float spine_animation_state_data_get_mix_by_name(spine_animation_state_data stateData, const utf8 *fromName, const utf8 *toName) {
	if (stateData == nullptr) return 0;
	AnimationStateData *_stateData = (AnimationStateData *) stateData;
	Animation *from = _stateData->getSkeletonData()->findAnimation(fromName);
	Animation *to = _stateData->getSkeletonData()->findAnimation(toName);
	if (from == nullptr || to == nullptr) return 0;
	return _stateData->getMix(from, to);
}

void spine_animation_state_data_clear(spine_animation_state_data stateData) {
	if (stateData == nullptr) return;
	AnimationStateData *_stateData = (AnimationStateData *) stateData;
	_stateData->clear();
}

// AnimationState
void spine_animation_state_update(spine_animation_state state, float delta) {
	if (state == nullptr) return;
	AnimationState *_state = (AnimationState *) state;
	_state->update(delta);
}

void spine_animation_state_dispose_track_entry(spine_animation_state state, spine_track_entry entry) {
	if (state == nullptr) return;
	if (entry == nullptr) return;
	AnimationState *_state = (AnimationState *) state;
	_state->disposeTrackEntry((TrackEntry *) entry);
}

void spine_animation_state_apply(spine_animation_state state, spine_skeleton skeleton) {
	if (state == nullptr) return;
	AnimationState *_state = (AnimationState *) state;
	_state->apply(*(Skeleton *) skeleton);
}

void spine_animation_state_clear_tracks(spine_animation_state state) {
	if (state == nullptr) return;
	AnimationState *_state = (AnimationState *) state;
	_state->clearTracks();
}

int32_t spine_animation_state_get_num_tracks(spine_animation_state state) {
	if (state == nullptr) return 0;
	AnimationState *_state = (AnimationState *) state;
	return (int32_t) _state->getTracks().size();
}

void spine_animation_state_clear_track(spine_animation_state state, int32_t trackIndex) {
	if (state == nullptr) return;
	AnimationState *_state = (AnimationState *) state;
	_state->clearTrack(trackIndex);
}

spine_track_entry spine_animation_state_set_animation_by_name(spine_animation_state state, int32_t trackIndex, const utf8 *animationName, int32_t loop) {
	if (state == nullptr) return nullptr;
	AnimationState *_state = (AnimationState *) state;
	return (spine_track_entry) _state->setAnimation(trackIndex, animationName, loop);
}

spine_track_entry spine_animation_state_set_animation(spine_animation_state state, int32_t trackIndex, spine_animation animation, int32_t loop) {
	if (state == nullptr) return nullptr;
	AnimationState *_state = (AnimationState *) state;
	return (spine_track_entry) _state->setAnimation(trackIndex, (Animation *) animation, loop);
}

spine_track_entry spine_animation_state_add_animation_by_name(spine_animation_state state, int32_t trackIndex, const utf8 *animationName, int32_t loop, float delay) {
	if (state == nullptr) return nullptr;
	AnimationState *_state = (AnimationState *) state;
	return (spine_track_entry) _state->addAnimation(trackIndex, animationName, loop, delay);
}

spine_track_entry spine_animation_state_add_animation(spine_animation_state state, int32_t trackIndex, spine_animation animation, int32_t loop, float delay) {
	if (state == nullptr) return nullptr;
	AnimationState *_state = (AnimationState *) state;
	return (spine_track_entry) _state->addAnimation(trackIndex, (Animation *) animation, loop, delay);
}

spine_track_entry spine_animation_state_set_empty_animation(spine_animation_state state, int32_t trackIndex, float mixDuration) {
	if (state == nullptr) return nullptr;
	AnimationState *_state = (AnimationState *) state;
	return (spine_track_entry) _state->setEmptyAnimation(trackIndex, mixDuration);
}

spine_track_entry spine_animation_state_add_empty_animation(spine_animation_state state, int32_t trackIndex, float mixDuration, float delay) {
	if (state == nullptr) return nullptr;
	AnimationState *_state = (AnimationState *) state;
	return (spine_track_entry) _state->addEmptyAnimation(trackIndex, mixDuration, delay);
}

void spine_animation_state_set_empty_animations(spine_animation_state state, float mixDuration) {
	if (state == nullptr) return;
	AnimationState *_state = (AnimationState *) state;
	_state->setEmptyAnimations(mixDuration);
}

spine_track_entry spine_animation_state_get_current(spine_animation_state state, int32_t trackIndex) {
	if (state == nullptr) return nullptr;
	AnimationState *_state = (AnimationState *) state;
	return (spine_track_entry) _state->getCurrent(trackIndex);
}

spine_animation_state_data spine_animation_state_get_data(spine_animation_state state) {
	if (state == nullptr) return nullptr;
	AnimationState *_state = (AnimationState *) state;
	return (spine_animation_state_data) _state->getData();
}

float spine_animation_state_get_time_scale(spine_animation_state state) {
	if (state == nullptr) return 0;
	AnimationState *_state = (AnimationState *) state;
	return _state->getTimeScale();
}

void spine_animation_state_set_time_scale(spine_animation_state state, float timeScale) {
	if (state == nullptr) return;
	AnimationState *_state = (AnimationState *) state;
	_state->setTimeScale(timeScale);
}

int32_t spine_animation_state_events_get_num_events(spine_animation_state_events events) {
	if (events == nullptr) return 0;
	EventListener *_events = (EventListener *) events;
	return (int32_t) _events->events.size();
}

spine_event_type spine_animation_state_events_get_event_type(spine_animation_state_events events, int32_t index) {
	if (events == nullptr) return SPINE_EVENT_TYPE_DISPOSE;
	if (index < 0) return SPINE_EVENT_TYPE_DISPOSE;
	EventListener *_events = (EventListener *) events;
	if (index >= _events->events.size()) return SPINE_EVENT_TYPE_DISPOSE;
	return (spine_event_type) _events->events[index].type;
}

spine_track_entry spine_animation_state_events_get_track_entry(spine_animation_state_events events, int32_t index) {
	if (events == nullptr) return nullptr;
	EventListener *_events = (EventListener *) events;
	if (index >= _events->events.size()) return nullptr;
	return (spine_track_entry) _events->events[index].entry;
}

spine_event spine_animation_state_events_get_event(spine_animation_state_events events, int32_t index) {
	if (events == nullptr) return nullptr;
	EventListener *_events = (EventListener *) events;
	if (index >= _events->events.size()) return nullptr;
	return (spine_event) _events->events[index].event;
}

void spine_animation_state_events_reset(spine_animation_state_events events) {
	if (events == nullptr) return;
	EventListener *_events = (EventListener *) events;
	_events->events.clear();
}

// TrackEntry

int32_t spine_track_entry_get_track_index(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getTrackIndex();
}

spine_animation spine_track_entry_get_animation(spine_track_entry entry) {
	if (entry == nullptr) return nullptr;
	TrackEntry *_entry = (TrackEntry *) entry;
	return (spine_animation) _entry->getAnimation();
}

spine_track_entry spine_track_entry_get_previous(spine_track_entry entry) {
	if (entry == nullptr) return nullptr;
	TrackEntry *_entry = (TrackEntry *) entry;
	return (spine_track_entry) _entry->getPrevious();
}

int32_t spine_track_entry_get_loop(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getLoop() ? -1 : 0;
}

void spine_track_entry_set_loop(spine_track_entry entry, int32_t loop) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setLoop(loop);
}

int32_t spine_track_entry_get_hold_previous(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getHoldPrevious() ? -1 : 0;
}

void spine_track_entry_set_hold_previous(spine_track_entry entry, int32_t holdPrevious) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setHoldPrevious(holdPrevious);
}

int32_t spine_track_entry_get_reverse(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getReverse() ? -1 : 0;
}

void spine_track_entry_set_reverse(spine_track_entry entry, int32_t reverse) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setReverse(reverse);
}

int32_t spine_track_entry_get_shortest_rotation(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getShortestRotation() ? -1 : 0;
}

void spine_track_entry_set_shortest_rotation(spine_track_entry entry, int32_t shortestRotation) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setShortestRotation(shortestRotation);
}

float spine_track_entry_get_delay(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getDelay();
}

void spine_track_entry_set_delay(spine_track_entry entry, float delay) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setDelay(delay);
}

float spine_track_entry_get_track_time(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getTrackTime();
}

void spine_track_entry_set_track_time(spine_track_entry entry, float trackTime) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setTrackTime(trackTime);
}

float spine_track_entry_get_track_end(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getTrackEnd();
}

void spine_track_entry_set_track_end(spine_track_entry entry, float trackEnd) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setTrackEnd(trackEnd);
}

float spine_track_entry_get_animation_start(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getAnimationStart();
}

void spine_track_entry_set_animation_start(spine_track_entry entry, float animationStart) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setAnimationStart(animationStart);
}

float spine_track_entry_get_animation_end(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getAnimationEnd();
}

void spine_track_entry_set_animation_end(spine_track_entry entry, float animationEnd) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setAnimationEnd(animationEnd);
}

float spine_track_entry_get_animation_last(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getAnimationLast();
}

void spine_track_entry_set_animation_last(spine_track_entry entry, float animationLast) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setAnimationLast(animationLast);
}

float spine_track_entry_get_animation_time(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getAnimationTime();
}

float spine_track_entry_get_time_scale(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getTimeScale();
}

void spine_track_entry_set_time_scale(spine_track_entry entry, float timeScale) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setTimeScale(timeScale);
}

float spine_track_entry_get_alpha(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getAlpha();
}

void spine_track_entry_set_alpha(spine_track_entry entry, float alpha) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setAlpha(alpha);
}

float spine_track_entry_get_event_threshold(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getEventThreshold();
}

void spine_track_entry_set_event_threshold(spine_track_entry entry, float eventThreshold) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setEventThreshold(eventThreshold);
}

float spine_track_entry_get_attachment_threshold(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getAttachmentThreshold();
}

void spine_track_entry_set_attachment_threshold(spine_track_entry entry, float attachmentThreshold) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setAttachmentThreshold(attachmentThreshold);
}

float spine_track_entry_get_draw_order_threshold(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getDrawOrderThreshold();
}

void spine_track_entry_set_draw_order_threshold(spine_track_entry entry, float drawOrderThreshold) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setDrawOrderThreshold(drawOrderThreshold);
}

spine_track_entry spine_track_entry_get_next(spine_track_entry entry) {
	if (entry == nullptr) return nullptr;
	TrackEntry *_entry = (TrackEntry *) entry;
	return (spine_track_entry) _entry->getNext();
}

int32_t spine_track_entry_is_complete(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->isComplete() ? -1 : 0;
}

float spine_track_entry_get_mix_time(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getMixTime();
}

void spine_track_entry_set_mix_time(spine_track_entry entry, float mixTime) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setMixTime(mixTime);
}

float spine_track_entry_get_mix_duration(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getMixDuration();
}

void spine_track_entry_set_mix_duration(spine_track_entry entry, float mixDuration) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setMixDuration(mixDuration);
}

spine_mix_blend spine_track_entry_get_mix_blend(spine_track_entry entry) {
	if (entry == nullptr) return SPINE_MIX_BLEND_SETUP;
	TrackEntry *_entry = (TrackEntry *) entry;
	return (spine_mix_blend) _entry->getMixBlend();
}

void spine_track_entry_set_mix_blend(spine_track_entry entry, spine_mix_blend mixBlend) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->setMixBlend((MixBlend) mixBlend);
}

spine_track_entry spine_track_entry_get_mixing_from(spine_track_entry entry) {
	if (entry == nullptr) return nullptr;
	TrackEntry *_entry = (TrackEntry *) entry;
	return (spine_track_entry) _entry->getMixingFrom();
}

spine_track_entry spine_track_entry_get_mixing_to(spine_track_entry entry) {
	if (entry == nullptr) return nullptr;
	TrackEntry *_entry = (TrackEntry *) entry;
	return (spine_track_entry) _entry->getMixingTo();
}

void spine_track_entry_reset_rotation_directions(spine_track_entry entry) {
	if (entry == nullptr) return;
	TrackEntry *_entry = (TrackEntry *) entry;
	_entry->resetRotationDirections();
}

float spine_track_entry_get_track_complete(spine_track_entry entry) {
	if (entry == nullptr) return 0;
	TrackEntry *_entry = (TrackEntry *) entry;
	return _entry->getTrackComplete();
}

// Skeleton

void spine_skeleton_update_cache(spine_skeleton skeleton) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->updateCache();
}

void spine_skeleton_update_world_transform(spine_skeleton skeleton) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->updateWorldTransform();
}

void spine_skeleton_update_world_transform_bone(spine_skeleton skeleton, spine_bone parent) {
	if (skeleton == nullptr) return;
	if (parent == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	Bone *_bone = (Bone *) parent;
	_skeleton->updateWorldTransform(_bone);
}

void spine_skeleton_set_to_setup_pose(spine_skeleton skeleton) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->setToSetupPose();
}

void spine_skeleton_set_bones_to_setup_pose(spine_skeleton skeleton) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->setBonesToSetupPose();
}

void spine_skeleton_set_slots_to_setup_pose(spine_skeleton skeleton) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->setSlotsToSetupPose();
}

spine_bone spine_skeleton_find_bone(spine_skeleton skeleton, const utf8 *boneName) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_bone) _skeleton->findBone(boneName);
}

spine_slot spine_skeleton_find_slot(spine_skeleton skeleton, const utf8 *slotName) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_slot) _skeleton->findSlot(slotName);
}

void spine_skeleton_set_skin_by_name(spine_skeleton skeleton, const utf8 *skinName) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->setSkin(skinName);
}

void spine_skeleton_set_skin(spine_skeleton skeleton, spine_skin skin) {
	if (skeleton == nullptr) return;
	if (skin == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->setSkin((Skin *) skin);
}

spine_attachment spine_skeleton_get_attachment_by_name(spine_skeleton skeleton, const utf8 *slotName, const utf8 *attachmentName) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_attachment) _skeleton->getAttachment(slotName, attachmentName);
}

spine_attachment spine_skeleton_get_attachment(spine_skeleton skeleton, int32_t slotIndex, const utf8 *attachmentName) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_attachment) _skeleton->getAttachment(slotIndex, attachmentName);
}

void spine_skeleton_set_attachment(spine_skeleton skeleton, const utf8 *slotName, const utf8 *attachmentName) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return _skeleton->setAttachment(slotName, attachmentName);
}

spine_ik_constraint spine_skeleton_find_ik_constraint(spine_skeleton skeleton, const utf8 *constraintName) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_ik_constraint) _skeleton->findIkConstraint(constraintName);
}

spine_transform_constraint spine_skeleton_find_transform_constraint(spine_skeleton skeleton, const utf8 *constraintName) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_transform_constraint) _skeleton->findTransformConstraint(constraintName);
}

spine_path_constraint spine_skeleton_find_path_constraint(spine_skeleton skeleton, const utf8 *constraintName) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_path_constraint) _skeleton->findPathConstraint(constraintName);
}

spine_bounds spine_skeleton_get_bounds(spine_skeleton skeleton) {
	_spine_bounds *bounds = (_spine_bounds*)malloc(sizeof(_spine_bounds));
	if (skeleton == nullptr) return (spine_bounds) bounds;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	Vector<float> vertices;
	_skeleton->getBounds(bounds->x, bounds->y, bounds->width, bounds->height, vertices);
	return (spine_bounds) bounds;
}

spine_bone spine_skeleton_get_root_bone(spine_skeleton skeleton) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_bone) _skeleton->getRootBone();
}

spine_skeleton_data spine_skeleton_get_data(spine_skeleton skeleton) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_skeleton_data) _skeleton->getData();
}

int32_t spine_skeleton_get_num_bones(spine_skeleton skeleton) {
	if (skeleton == nullptr) return 0;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (int32_t) _skeleton->getBones().size();
}

spine_bone *spine_skeleton_get_bones(spine_skeleton skeleton) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_bone *) _skeleton->getBones().buffer();
}

int32_t spine_skeleton_get_num_slots(spine_skeleton skeleton) {
	if (skeleton == nullptr) return 0;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (int32_t) _skeleton->getSlots().size();
}

spine_slot *spine_skeleton_get_slots(spine_skeleton skeleton) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_slot *) _skeleton->getSlots().buffer();
}

int32_t spine_skeleton_get_num_draw_order(spine_skeleton skeleton) {
	if (skeleton == nullptr) return 0;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (int32_t) _skeleton->getDrawOrder().size();
}

spine_slot *spine_skeleton_get_draw_order(spine_skeleton skeleton) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_slot *) _skeleton->getDrawOrder().buffer();
}

int32_t spine_skeleton_get_num_ik_constraints(spine_skeleton skeleton) {
	if (skeleton == nullptr) return 0;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (int32_t) _skeleton->getIkConstraints().size();
}

spine_ik_constraint *spine_skeleton_get_ik_constraints(spine_skeleton skeleton) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_ik_constraint *) _skeleton->getIkConstraints().buffer();
}

int32_t spine_skeleton_get_num_transform_constraints(spine_skeleton skeleton) {
	if (skeleton == nullptr) return 0;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (int32_t) _skeleton->getTransformConstraints().size();
}

spine_transform_constraint *spine_skeleton_get_transform_constraints(spine_skeleton skeleton) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_transform_constraint *) _skeleton->getTransformConstraints().buffer();
}

int32_t spine_skeleton_get_num_path_constraints(spine_skeleton skeleton) {
	if (skeleton == nullptr) return 0;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (int32_t) _skeleton->getPathConstraints().size();
}

spine_path_constraint *spine_skeleton_get_path_constraints(spine_skeleton skeleton) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_path_constraint *) _skeleton->getPathConstraints().buffer();
}

spine_skin spine_skeleton_get_skin(spine_skeleton skeleton) {
	if (skeleton == nullptr) return nullptr;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_skin) _skeleton->getSkin();
}

spine_color spine_skeleton_get_color(spine_skeleton skeleton) {
	if (skeleton == nullptr) return (spine_color) &NULL_COLOR;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return (spine_color) &_skeleton->getColor();
}

void spine_skeleton_set_color(spine_skeleton skeleton, float r, float g, float b, float a) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->getColor().set(r, g, b, a);
}

void spine_skeleton_set_position(spine_skeleton skeleton, float x, float y) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->setPosition(x, y);
}

float spine_skeleton_get_x(spine_skeleton skeleton) {
	if (skeleton == nullptr) return 0;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return _skeleton->getX();
}

void spine_skeleton_set_x(spine_skeleton skeleton, float x) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->setX(x);
}

float spine_skeleton_get_y(spine_skeleton skeleton) {
	if (skeleton == nullptr) return 0;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return _skeleton->getY();
}

void spine_skeleton_set_y(spine_skeleton skeleton, float y) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->setY(y);
}

float spine_skeleton_get_scale_x(spine_skeleton skeleton) {
	if (skeleton == nullptr) return 0;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return _skeleton->getScaleX();
}

void spine_skeleton_set_scale_x(spine_skeleton skeleton, float scaleX) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->setScaleX(scaleX);
}

float spine_skeleton_get_scale_y(spine_skeleton skeleton) {
	if (skeleton == nullptr) return 0;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	return _skeleton->getScaleY();
}

void spine_skeleton_set_scale_y(spine_skeleton skeleton, float scaleY) {
	if (skeleton == nullptr) return;
	Skeleton *_skeleton = (Skeleton *) skeleton;
	_skeleton->setScaleY(scaleY);
}

// EventData

const utf8 *spine_event_data_get_name(spine_event_data event) {
	if (event == nullptr) return nullptr;
	EventData *_event = (EventData *) event;
	return (utf8 *) _event->getName().buffer();
}

int32_t spine_event_data_get_int_value(spine_event_data event) {
	if (event == nullptr) return 0;
	EventData *_event = (EventData *) event;
	return _event->getIntValue();
}

void spine_event_data_set_int_value(spine_event_data event, int32_t value) {
	if (event == nullptr) return;
	EventData *_event = (EventData *) event;
	_event->setIntValue(value);
}

float spine_event_data_get_float_value(spine_event_data event) {
	if (event == nullptr) return 0;
	EventData *_event = (EventData *) event;
	return _event->getFloatValue();
}

void spine_event_data_set_float_value(spine_event_data event, float value) {
	if (event == nullptr) return;
	EventData *_event = (EventData *) event;
	_event->setFloatValue(value);
}

const utf8 *spine_event_data_get_string_value(spine_event_data event) {
	if (event == nullptr) return nullptr;
	EventData *_event = (EventData *) event;
	return (utf8 *) _event->getStringValue().buffer();
}

void spine_event_data_set_string_value(spine_event_data event, const utf8 *value) {
	if (event == nullptr) return;
	EventData *_event = (EventData *) event;
	_event->setStringValue(value);
}

const utf8 *spine_event_data_get_audio_path(spine_event_data event) {
	if (event == nullptr) return nullptr;
	EventData *_event = (EventData *) event;
	return (utf8 *) _event->getAudioPath().buffer();
}

float spine_event_data_get_volume(spine_event_data event) {
	if (event == nullptr) return 0;
	EventData *_event = (EventData *) event;
	return _event->getVolume();
}

void spine_event_data_set_volume(spine_event_data event, float volume) {
	if (event == nullptr) return;
	EventData *_event = (EventData *) event;
	_event->setVolume(volume);
}

float spine_event_data_get_balance(spine_event_data event) {
	if (event == nullptr) return 0;
	EventData *_event = (EventData *) event;
	return _event->getBalance();
}

void spine_event_data_set_balance(spine_event_data event, float balance) {
	if (event == nullptr) return;
	EventData *_event = (EventData *) event;
	_event->setBalance(balance);
}

// Event

spine_event_data spine_event_get_data(spine_event event) {
	if (event == nullptr) return nullptr;
	Event *_event = (Event *) event;
	return (spine_event_data) &_event->getData();
}

float spine_event_get_time(spine_event event) {
	if (event == nullptr) return 0;
	Event *_event = (Event *) event;
	return _event->getTime();
}

int32_t spine_event_get_int_value(spine_event event) {
	if (event == nullptr) return 0;
	Event *_event = (Event *) event;
	return _event->getIntValue();
}

void spine_event_set_int_value(spine_event event, int32_t value) {
	if (event == nullptr) return;
	Event *_event = (Event *) event;
	_event->setIntValue(value);
}

float spine_event_get_float_value(spine_event event) {
	if (event == nullptr) return 0;
	Event *_event = (Event *) event;
	return _event->getFloatValue();
}

void spine_event_set_float_value(spine_event event, float value) {
	if (event == nullptr) return;
	Event *_event = (Event *) event;
	_event->setFloatValue(value);
}

const utf8 *spine_event_get_string_value(spine_event event) {
	if (event == nullptr) return nullptr;
	Event *_event = (Event *) event;
	return (utf8 *) _event->getStringValue().buffer();
}

void spine_event_set_string_value(spine_event event, const utf8 *value) {
	if (event == nullptr) return;
	Event *_event = (Event *) event;
	_event->setStringValue(value);
}

float spine_event_get_volume(spine_event event) {
	if (event == nullptr) return 0;
	Event *_event = (Event *) event;
	return _event->getVolume();
}

void spine_event_set_volume(spine_event event, float volume) {
	if (event == nullptr) return;
	Event *_event = (Event *) event;
	_event->setVolume(volume);
}

float spine_event_get_balance(spine_event event) {
	if (event == nullptr) return 0;
	Event *_event = (Event *) event;
	return _event->getBalance();
}

void spine_event_set_balance(spine_event event, float balance) {
	if (event == nullptr) return;
	Event *_event = (Event *) event;
	_event->setBalance(balance);
}

// SlotData
int32_t spine_slot_data_get_index(spine_slot_data slot) {
	if (slot == nullptr) return 0;
	SlotData *_slot = (SlotData *) slot;
	return _slot->getIndex();
}

const utf8 *spine_slot_data_get_name(spine_slot_data slot) {
	if (slot == nullptr) return nullptr;
	SlotData *_slot = (SlotData *) slot;
	return (utf8 *) _slot->getName().buffer();
}

spine_bone_data spine_slot_data_get_bone_data(spine_slot_data slot) {
	if (slot == nullptr) return nullptr;
	SlotData *_slot = (SlotData *) slot;
	return (spine_bone_data) &_slot->getBoneData();
}

spine_color spine_slot_data_get_color(spine_slot_data slot) {
	if (slot == nullptr) return (spine_color) &NULL_COLOR;
	SlotData *_slot = (SlotData *) slot;
	return (spine_color) &_slot->getColor();
}

void spine_slot_data_set_color(spine_slot_data slot, float r, float g, float b, float a) {
	if (slot == nullptr) return;
	SlotData *_slot = (SlotData *) slot;
	_slot->getColor().set(r, g, b, a);
}

spine_color spine_slot_data_get_dark_color(spine_slot_data slot) {
	if (slot == nullptr) return (spine_color) &NULL_COLOR;
	SlotData *_slot = (SlotData *) slot;
	return (spine_color) &_slot->getDarkColor();
}

void spine_slot_data_set_dark_color(spine_slot_data slot, float r, float g, float b, float a) {
	if (slot == nullptr) return;
	SlotData *_slot = (SlotData *) slot;
	_slot->getDarkColor().set(r, g, b, a);
}

int32_t spine_slot_data_has_dark_color(spine_slot_data slot) {
	if (slot == nullptr) return 0;
	SlotData *_slot = (SlotData *) slot;
	return _slot->hasDarkColor() ? -1 : 0;
}

void spine_slot_data_set_has_dark_color(spine_slot_data slot, int32_t hasDarkColor) {
	if (slot == nullptr) return;
	SlotData *_slot = (SlotData *) slot;
	_slot->setHasDarkColor(hasDarkColor);
}

const utf8 *spine_slot_data_get_attachment_name(spine_slot_data slot) {
	if (slot == nullptr) return nullptr;
	SlotData *_slot = (SlotData *) slot;
	return (utf8 *) _slot->getAttachmentName().buffer();
}

void spine_slot_data_set_attachment_name(spine_slot_data slot, const utf8 *attachmentName) {
	if (slot == nullptr) return;
	SlotData *_slot = (SlotData *) slot;
	_slot->setAttachmentName(attachmentName);
}

spine_blend_mode spine_slot_data_get_blend_mode(spine_slot_data slot) {
	if (slot == nullptr) return SPINE_BLEND_MODE_NORMAL;
	SlotData *_slot = (SlotData *) slot;
	return (spine_blend_mode) _slot->getBlendMode();
}

// Slot
void spine_slot_set_to_setup_pose(spine_slot slot) {
	if (slot == nullptr) return;
	Slot *_slot = (Slot *) slot;
	_slot->setToSetupPose();
}

spine_slot_data spine_slot_get_data(spine_slot slot) {
	if (slot == nullptr) return nullptr;
	Slot *_slot = (Slot *) slot;
	return (spine_slot_data) &_slot->getData();
}

spine_bone spine_slot_get_bone(spine_slot slot) {
	if (slot == nullptr) return nullptr;
	Slot *_slot = (Slot *) slot;
	return (spine_bone) &_slot->getBone();
}

spine_skeleton spine_slot_get_skeleton(spine_slot slot) {
	if (slot == nullptr) return nullptr;
	Slot *_slot = (Slot *) slot;
	return (spine_skeleton) &_slot->getSkeleton();
}

spine_color spine_slot_get_color(spine_slot slot) {
	if (slot == nullptr) return (spine_color) &NULL_COLOR;
	Slot *_slot = (Slot *) slot;
	return (spine_color) &_slot->getColor();
}

void spine_slot_set_color(spine_slot slot, float r, float g, float b, float a) {
	if (slot == nullptr) return;
	Slot *_slot = (Slot *) slot;
	_slot->getColor().set(r, g, b, a);
}

spine_color spine_slot_get_dark_color(spine_slot slot) {
	if (slot == nullptr) return (spine_color) &NULL_COLOR;
	Slot *_slot = (Slot *) slot;
	return (spine_color) &_slot->getDarkColor();
}

void spine_slot_set_dark_color(spine_slot slot, float r, float g, float b, float a) {
	if (slot == nullptr) return;
	Slot *_slot = (Slot *) slot;
	_slot->getDarkColor().set(r, g, b, a);
}

int32_t spine_slot_has_dark_color(spine_slot slot) {
	if (slot == nullptr) return 0;
	Slot *_slot = (Slot *) slot;
	return _slot->hasDarkColor() ? -1 : 0;
}

spine_attachment spine_slot_get_attachment(spine_slot slot) {
	if (slot == nullptr) return nullptr;
	Slot *_slot = (Slot *) slot;
	return (spine_attachment) _slot->getAttachment();
}

void spine_slot_set_attachment(spine_slot slot, spine_attachment attachment) {
	if (slot == nullptr) return;
	Slot *_slot = (Slot *) slot;
	_slot->setAttachment((Attachment *) attachment);
}

int32_t spine_slot_get_sequence_index(spine_slot slot) {
	if (slot == nullptr) return 0;
	Slot *_slot = (Slot *) slot;
	return _slot->getSequenceIndex();
}

void spine_slot_set_sequence_index(spine_slot slot, int32_t sequenceIndex) {
	if (slot == nullptr) return;
	Slot *_slot = (Slot *) slot;
	_slot->setSequenceIndex(sequenceIndex);
}

// BoneData
int32_t spine_bone_data_get_index(spine_bone_data data) {
	if (data == nullptr) return 0;
	BoneData *_data = (BoneData *) data;
	return _data->getIndex();
}

const utf8 *spine_bone_data_get_name(spine_bone_data data) {
	if (data == nullptr) return nullptr;
	BoneData *_data = (BoneData *) data;
	return (utf8 *) _data->getName().buffer();
}

spine_bone_data spine_bone_data_get_parent(spine_bone_data data) {
	if (data == nullptr) return nullptr;
	BoneData *_data = (BoneData *) data;
	return (spine_bone_data) _data->getParent();
}

float spine_bone_data_get_length(spine_bone_data data) {
	if (data == nullptr) return 0;
	BoneData *_data = (BoneData *) data;
	return _data->getLength();
}

void spine_bone_data_set_length(spine_bone_data data, float length) {
	if (data == nullptr) return;
	BoneData *_data = (BoneData *) data;
	_data->setLength(length);
}

float spine_bone_data_get_x(spine_bone_data data) {
	if (data == nullptr) return 0;
	BoneData *_data = (BoneData *) data;
	return _data->getX();
}

void spine_bone_data_set_x(spine_bone_data data, float x) {
	if (data == nullptr) return;
	BoneData *_data = (BoneData *) data;
	_data->setX(x);
}

float spine_bone_data_get_y(spine_bone_data data) {
	if (data == nullptr) return 0;
	BoneData *_data = (BoneData *) data;
	return _data->getY();
}

void spine_bone_data_set_y(spine_bone_data data, float y) {
	if (data == nullptr) return;
	BoneData *_data = (BoneData *) data;
	_data->setY(y);
}

float spine_bone_data_get_rotation(spine_bone_data data) {
	if (data == nullptr) return 0;
	BoneData *_data = (BoneData *) data;
	return _data->getRotation();
}

void spine_bone_data_set_rotation(spine_bone_data data, float rotation) {
	if (data == nullptr) return;
	BoneData *_data = (BoneData *) data;
	_data->setRotation(rotation);
}

float spine_bone_data_get_scale_x(spine_bone_data data) {
	if (data == nullptr) return 0;
	BoneData *_data = (BoneData *) data;
	return _data->getScaleX();
}

void spine_bone_data_set_scale_x(spine_bone_data data, float scaleX) {
	if (data == nullptr) return;
	BoneData *_data = (BoneData *) data;
	_data->setScaleX(scaleX);
}

float spine_bone_data_get_scale_y(spine_bone_data data) {
	if (data == nullptr) return 0;
	BoneData *_data = (BoneData *) data;
	return _data->getScaleY();
}

void spine_bone_data_set_scale_y(spine_bone_data data, float scaleY) {
	if (data == nullptr) return;
	BoneData *_data = (BoneData *) data;
	_data->setScaleY(scaleY);
}

float spine_bone_data_get_shear_x(spine_bone_data data) {
	if (data == nullptr) return 0;
	BoneData *_data = (BoneData *) data;
	return _data->getShearX();
}

void spine_bone_data_set_shear_x(spine_bone_data data, float shearX) {
	if (data == nullptr) return;
	BoneData *_data = (BoneData *) data;
	_data->setShearX(shearX);
}

float spine_bone_data_get_shear_y(spine_bone_data data) {
	if (data == nullptr) return 0;
	BoneData *_data = (BoneData *) data;
	return _data->getShearY();
}

void spine_bone_data_set_shear_y(spine_bone_data data, float y) {
	if (data == nullptr) return;
	BoneData *_data = (BoneData *) data;
	_data->setShearY(y);
}

spine_transform_mode spine_bone_data_get_transform_mode(spine_bone_data data) {
	if (data == nullptr) return SPINE_TRANSFORM_MODE_NORMAL;
	BoneData *_data = (BoneData *) data;
	return (spine_transform_mode) _data->getTransformMode();
}

void spine_bone_data_set_transform_mode(spine_bone_data data, spine_transform_mode mode) {
	if (data == nullptr) return;
	BoneData *_data = (BoneData *) data;
	_data->setTransformMode((TransformMode) mode);
}

int32_t spine_bone_data_is_skin_required(spine_bone_data data) {
	if (data == nullptr) return 0;
	BoneData *_data = (BoneData *) data;
	return _data->isSkinRequired() ? -1 : 0;
}

void spine_bone_data_set_is_skin_required(spine_bone_data data, int32_t isSkinRequired) {
	if (data == nullptr) return;
	BoneData *_data = (BoneData *) data;
	_data->setSkinRequired(isSkinRequired);
}

spine_color spine_bone_data_get_color(spine_bone_data data) {
	if (data == nullptr) return (spine_color) &NULL_COLOR;
	BoneData *_data = (BoneData *) data;
	return (spine_color) &_data->getColor();
}

void spine_bone_data_set_color(spine_bone_data data, float r, float g, float b, float a) {
	if (data == nullptr) return;
	BoneData *_data = (BoneData *) data;
	_data->getColor().set(r, g, b, a);
}

// Bone
void spine_bone_set_is_y_down(int32_t yDown) {
	Bone::setYDown(yDown);
}

int32_t spine_bone_get_is_y_down() {
	return Bone::isYDown() ? -1 : 0;
}

void spine_bone_update(spine_bone bone) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->update();
}

void spine_bone_update_world_transform(spine_bone bone) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->updateWorldTransform();
}

void spine_bone_update_world_transform_with(spine_bone bone, float x, float y, float rotation, float scaleX, float scaleY, float shearX, float shearY) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->updateWorldTransform(x, y, rotation, scaleX, scaleY, shearX, shearY);
}

void spine_bone_set_to_setup_pose(spine_bone bone) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setToSetupPose();
}

spine_vector spine_bone_world_to_local(spine_bone bone, float worldX, float worldY) {
	_spine_vector *coords = SpineExtension::calloc<_spine_vector>(1, __FILE__, __LINE__);
	if (bone == nullptr) return (spine_vector) coords;
	Bone *_bone = (Bone *) bone;
	_bone->worldToLocal(worldX, worldY, coords->x, coords->y);
	return (spine_vector) coords;
}

spine_vector spine_bone_local_to_world(spine_bone bone, float localX, float localY) {
	_spine_vector *coords = SpineExtension::calloc<_spine_vector>(1, __FILE__, __LINE__);
	if (bone == nullptr) return (spine_vector) coords;
	Bone *_bone = (Bone *) bone;
	_bone->localToWorld(localX, localY, coords->x, coords->y);
	return (spine_vector) coords;
}

float spine_bone_world_to_local_rotation(spine_bone bone, float worldRotation) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->worldToLocalRotation(worldRotation);
}

float spine_bone_local_to_world_rotation(spine_bone bone, float localRotation) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->localToWorldRotation(localRotation);
}

void spine_bone_rotate_world(spine_bone bone, float degrees) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->rotateWorld(degrees);
}

float spine_bone_get_world_to_local_rotation_x(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getWorldToLocalRotationX();
}

float spine_bone_get_world_to_local_rotation_y(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getWorldToLocalRotationY();
}

spine_bone_data spine_bone_get_data(spine_bone bone) {
	if (bone == nullptr) return nullptr;
	Bone *_bone = (Bone *) bone;
	return (spine_bone_data) &_bone->getData();
}

spine_skeleton spine_bone_get_skeleton(spine_bone bone) {
	if (bone == nullptr) return nullptr;
	Bone *_bone = (Bone *) bone;
	return (spine_skeleton) &_bone->getSkeleton();
}

spine_bone spine_bone_get_parent(spine_bone bone) {
	if (bone == nullptr) return nullptr;
	Bone *_bone = (Bone *) bone;
	return (spine_bone) _bone->getParent();
}

int32_t spine_bone_get_num_children(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return (int32_t) _bone->getChildren().size();
}

spine_bone *spine_bone_get_children(spine_bone bone) {
	if (bone == nullptr) return nullptr;
	Bone *_bone = (Bone *) bone;
	return (spine_bone *) _bone->getChildren().buffer();
}

float spine_bone_get_x(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getX();
}

void spine_bone_set_x(spine_bone bone, float x) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setX(x);
}

float spine_bone_get_y(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getY();
}

void spine_bone_set_y(spine_bone bone, float y) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setY(y);
}

float spine_bone_get_rotation(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getRotation();
}

void spine_bone_set_rotation(spine_bone bone, float rotation) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setRotation(rotation);
}

float spine_bone_get_scale_x(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getScaleX();
}

void spine_bone_set_scale_x(spine_bone bone, float scaleX) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setScaleX(scaleX);
}

float spine_bone_get_scale_y(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getScaleY();
}

void spine_bone_set_scale_y(spine_bone bone, float scaleY) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setScaleY(scaleY);
}

float spine_bone_get_shear_x(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getShearX();
}

void spine_bone_set_shear_x(spine_bone bone, float shearX) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setShearX(shearX);
}

float spine_bone_get_shear_y(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getShearY();
}

void spine_bone_set_shear_y(spine_bone bone, float shearY) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setShearY(shearY);
}

float spine_bone_get_applied_rotation(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getAppliedRotation();
}

void spine_bone_set_applied_rotation(spine_bone bone, float rotation) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setAppliedRotation(rotation);
}

float spine_bone_get_a_x(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getAX();
}

void spine_bone_set_a_x(spine_bone bone, float x) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setAX(x);
}

float spine_bone_get_a_y(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getAY();
}

void spine_bone_set_a_y(spine_bone bone, float y) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setAY(y);
}

float spine_bone_get_a_scale_x(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getAScaleX();
}

void spine_bone_set_a_scale_x(spine_bone bone, float scaleX) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setAScaleX(scaleX);
}

float spine_bone_get_a_scale_y(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getAScaleY();
}

void spine_bone_set_a_scale_y(spine_bone bone, float scaleY) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setAScaleY(scaleY);
}

float spine_bone_get_a_shear_x(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getAShearX();
}

void spine_bone_set_a_shear_x(spine_bone bone, float shearX) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setAShearX(shearX);
}

float spine_bone_get_a_shear_y(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getAShearY();
}

void spine_bone_set_shear_a_y(spine_bone bone, float shearY) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setAShearY(shearY);
}

float spine_bone_get_a(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getA();
}

void spine_bone_set_a(spine_bone bone, float a) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setA(a);
}

float spine_bone_get_b(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getB();
}

void spine_bone_set_b(spine_bone bone, float b) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setB(b);
}

float spine_bone_get_c(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getC();
}

void spine_bone_set_c(spine_bone bone, float c) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setC(c);
}

float spine_bone_get_d(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getD();
}

void spine_bone_set_d(spine_bone bone, float d) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setD(d);
}

float spine_bone_get_world_x(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getWorldX();
}

void spine_bone_set_world_x(spine_bone bone, float worldX) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setWorldX(worldX);
}

float spine_bone_get_world_y(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getWorldY();
}

void spine_bone_set_world_y(spine_bone bone, float worldY) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setWorldY(worldY);
}

float spine_bone_get_world_rotation_x(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getWorldRotationX();
}

float spine_bone_get_world_rotation_y(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getWorldToLocalRotationY();
}

float spine_bone_get_world_scale_x(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getWorldScaleX();
}

float spine_bone_get_world_scale_y(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->getWorldScaleY();
}

int32_t spine_bone_get_is_active(spine_bone bone) {
	if (bone == nullptr) return 0;
	Bone *_bone = (Bone *) bone;
	return _bone->isActive() ? -1 : 0;
}

void spine_bone_set_is_active(spine_bone bone, int32_t isActive) {
	if (bone == nullptr) return;
	Bone *_bone = (Bone *) bone;
	_bone->setActive(isActive);
}

// Attachment
const utf8 *spine_attachment_get_name(spine_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	Attachment *_attachment = (Attachment *) attachment;
	return (utf8 *) _attachment->getName().buffer();
}

spine_attachment_type spine_attachment_get_type(spine_attachment attachment) {
	if (attachment == nullptr) return SPINE_ATTACHMENT_REGION;
	Attachment *_attachment = (Attachment *) attachment;
	if (_attachment->getRTTI().isExactly(RegionAttachment::rtti)) {
		return SPINE_ATTACHMENT_REGION;
	} else if (_attachment->getRTTI().isExactly(MeshAttachment::rtti)) {
		return SPINE_ATTACHMENT_MESH;
	} else if (_attachment->getRTTI().isExactly(ClippingAttachment::rtti)) {
		return SPINE_ATTACHMENT_CLIPPING;
	} else if (_attachment->getRTTI().isExactly(BoundingBoxAttachment::rtti)) {
		return SPINE_ATTACHMENT_BOUNDING_BOX;
	} else if (_attachment->getRTTI().isExactly(PathAttachment::rtti)) {
		return SPINE_ATTACHMENT_PATH;
	} else if (_attachment->getRTTI().isExactly(PointAttachment::rtti)) {
		return SPINE_ATTACHMENT_POINT;
	} else {
		return SPINE_ATTACHMENT_REGION;
	}
}

spine_attachment spine_attachment_copy(spine_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	Attachment *_attachment = (Attachment *) attachment;
	return (spine_attachment) _attachment->copy();
}

void spine_attachment_dispose(spine_attachment attachment) {
	if (attachment == nullptr) return;
	Attachment *_attachment = (Attachment *) attachment;
	delete _attachment;
}

// PointAttachment
spine_vector spine_point_attachment_compute_world_position(spine_point_attachment attachment, spine_bone bone) {
	_spine_vector *result = SpineExtension::calloc<_spine_vector>(1, __FILE__, __LINE__);
	if (attachment == nullptr) return (spine_vector) result;
	PointAttachment *_attachment = (PointAttachment *) attachment;
	_attachment->computeWorldPosition(*(Bone *) bone, result->x, result->y);
	return (spine_vector) result;
}

float spine_point_attachment_compute_world_rotation(spine_point_attachment attachment, spine_bone bone) {
	if (attachment == nullptr) return 0;
	PointAttachment *_attachment = (PointAttachment *) attachment;
	return _attachment->computeWorldRotation(*(Bone *) bone);
}

float spine_point_attachment_get_x(spine_point_attachment attachment) {
	if (attachment == nullptr) return 0;
	PointAttachment *_attachment = (PointAttachment *) attachment;
	return _attachment->getX();
}

void spine_point_attachment_set_x(spine_point_attachment attachment, float x) {
	if (attachment == nullptr) return;
	PointAttachment *_attachment = (PointAttachment *) attachment;
	_attachment->setX(x);
}

float spine_point_attachment_get_y(spine_point_attachment attachment) {
	if (attachment == nullptr) return 0;
	PointAttachment *_attachment = (PointAttachment *) attachment;
	return _attachment->getY();
}

void spine_point_attachment_set_y(spine_point_attachment attachment, float y) {
	if (attachment == nullptr) return;
	PointAttachment *_attachment = (PointAttachment *) attachment;
	_attachment->setY(y);
}

float spine_point_attachment_get_rotation(spine_point_attachment attachment) {
	if (attachment == nullptr) return 0;
	PointAttachment *_attachment = (PointAttachment *) attachment;
	return _attachment->getRotation();
}

void spine_point_attachment_set_rotation(spine_point_attachment attachment, float rotation) {
	if (attachment == nullptr) return;
	PointAttachment *_attachment = (PointAttachment *) attachment;
	_attachment->setRotation(rotation);
}

spine_color spine_point_attachment_get_color(spine_point_attachment attachment) {
	if (attachment == nullptr) return (spine_color) &NULL_COLOR;
	PointAttachment *_attachment = (PointAttachment *) attachment;
	return (spine_color) &_attachment->getColor();
}

void spine_point_attachment_set_color(spine_point_attachment attachment, float r, float g, float b, float a) {
	if (attachment == nullptr) return;
	PointAttachment *_attachment = (PointAttachment *) attachment;
	_attachment->getColor().set(r, g, b, a);
}

// RegionAttachment
void spine_region_attachment_update_region(spine_region_attachment attachment) {
	if (attachment == nullptr) return;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	_attachment->updateRegion();
}

void spine_region_attachment_compute_world_vertices(spine_region_attachment attachment, spine_slot slot, float *worldVertices) {
	if (attachment == nullptr) return;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	_attachment->computeWorldVertices(*(Slot *) slot, worldVertices, 0);
}

float spine_region_attachment_get_x(spine_region_attachment attachment) {
	if (attachment == nullptr) return 0;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return _attachment->getX();
}

void spine_region_attachment_set_x(spine_region_attachment attachment, float x) {
	if (attachment == nullptr) return;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	_attachment->setX(x);
}

float spine_region_attachment_get_y(spine_region_attachment attachment) {
	if (attachment == nullptr) return 0;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return _attachment->getY();
}

void spine_region_attachment_set_y(spine_region_attachment attachment, float y) {
	if (attachment == nullptr) return;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	_attachment->setY(y);
}

float spine_region_attachment_get_rotation(spine_region_attachment attachment) {
	if (attachment == nullptr) return 0;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return _attachment->getRotation();
}

void spine_region_attachment_set_rotation(spine_region_attachment attachment, float rotation) {
	if (attachment == nullptr) return;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	_attachment->setRotation(rotation);
}

float spine_region_attachment_get_scale_x(spine_region_attachment attachment) {
	if (attachment == nullptr) return 0;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return _attachment->getScaleX();
}

void spine_region_attachment_set_scale_x(spine_region_attachment attachment, float scaleX) {
	if (attachment == nullptr) return;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	_attachment->setScaleX(scaleX);
}

float spine_region_attachment_get_scale_y(spine_region_attachment attachment) {
	if (attachment == nullptr) return 0;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return _attachment->getScaleY();
}

void spine_region_attachment_set_scale_y(spine_region_attachment attachment, float scaleY) {
	if (attachment == nullptr) return;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	_attachment->setScaleY(scaleY);
}

float spine_region_attachment_get_width(spine_region_attachment attachment) {
	if (attachment == nullptr) return 0;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return _attachment->getWidth();
}

void spine_region_attachment_set_width(spine_region_attachment attachment, float width) {
	if (attachment == nullptr) return;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	_attachment->setWidth(width);
}

float spine_region_attachment_get_height(spine_region_attachment attachment) {
	if (attachment == nullptr) return 0;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return _attachment->getHeight();
}

void spine_region_attachment_set_height(spine_region_attachment attachment, float height) {
	if (attachment == nullptr) return;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	_attachment->setHeight(height);
}

spine_color spine_region_attachment_get_color(spine_region_attachment attachment) {
	if (attachment == nullptr) return (spine_color) &NULL_COLOR;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return (spine_color) &_attachment->getColor();
}

void spine_region_attachment_set_color(spine_region_attachment attachment, float r, float g, float b, float a) {
	if (attachment == nullptr) return;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	_attachment->getColor().set(r, g, b, a);
}

const utf8 *spine_region_attachment_get_path(spine_region_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return (utf8 *) _attachment->getPath().buffer();
}

spine_texture_region spine_region_attachment_get_region(spine_region_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return (spine_texture_region) _attachment->getRegion();
}

spine_sequence spine_region_attachment_get_sequence(spine_region_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return (spine_sequence) _attachment->getSequence();
}

int32_t spine_region_attachment_get_num_offset(spine_region_attachment attachment) {
	if (attachment == nullptr) return 0;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return (int32_t) _attachment->getOffset().size();
}

float *spine_region_attachment_get_offset(spine_region_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return _attachment->getOffset().buffer();
}

int32_t spine_region_attachment_get_num_uvs(spine_region_attachment attachment) {
	if (attachment == nullptr) return 0;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return (int32_t) _attachment->getUVs().size();
}

float *spine_region_attachment_get_uvs(spine_region_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	RegionAttachment *_attachment = (RegionAttachment *) attachment;
	return _attachment->getUVs().buffer();
}

// VertexAttachment
int32_t spine_vertex_attachment_get_world_vertices_length(spine_vertex_attachment attachment) {
	if (attachment == nullptr) return 0;
	VertexAttachment *_attachment = (VertexAttachment *) attachment;
	return (int32_t) _attachment->getWorldVerticesLength();
}

void spine_vertex_attachment_compute_world_vertices(spine_vertex_attachment attachment, spine_slot slot, float *worldVertices) {
	if (attachment == nullptr) return;
	VertexAttachment *_attachment = (VertexAttachment *) attachment;
	_attachment->computeWorldVertices(*(Slot *) slot, worldVertices);
}

int32_t spine_vertex_attachment_get_num_bones(spine_vertex_attachment attachment) {
	if (attachment == nullptr) return 0;
	VertexAttachment *_attachment = (VertexAttachment *) attachment;
	return (int32_t) _attachment->getBones().size();
}

int32_t *spine_vertex_attachment_get_bones(spine_vertex_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	VertexAttachment *_attachment = (VertexAttachment *) attachment;
	return _attachment->getBones().buffer();
}

// VertexAttachment
int32_t spine_vertex_attachment_get_num_vertices(spine_vertex_attachment attachment) {
	if (attachment == nullptr) return 0;
	VertexAttachment *_attachment = (VertexAttachment *) attachment;
	return (int32_t) _attachment->getVertices().size();
}

float *spine_vertex_attachment_get_vertices(spine_vertex_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	VertexAttachment *_attachment = (VertexAttachment *) attachment;
	return _attachment->getVertices().buffer();
}

spine_attachment spine_vertex_attachment_get_timeline_attachment(spine_vertex_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	VertexAttachment *_attachment = (VertexAttachment *) attachment;
	return (spine_attachment) _attachment->getTimelineAttachment();
}

void spine_vertex_attachment_set_timeline_attachment(spine_vertex_attachment attachment, spine_attachment timelineAttachment) {
	if (attachment == nullptr) return;
	VertexAttachment *_attachment = (VertexAttachment *) attachment;
	_attachment->setTimelineAttachment((Attachment *) timelineAttachment);
}

// MeshAttachment
void spine_mesh_attachment_update_region(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	_attachment->updateRegion();
}

int32_t spine_mesh_attachment_get_hull_length(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return 0;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return _attachment->getHullLength();
}

void spine_mesh_attachment_set_hull_length(spine_mesh_attachment attachment, int32_t hullLength) {
	if (attachment == nullptr) return;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	_attachment->setHullLength(hullLength);
}

int32_t spine_mesh_attachment_get_num_region_uvs(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return 0;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return (int32_t) _attachment->getRegionUVs().size();
}

float *spine_mesh_attachment_get_region_uvs(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return _attachment->getRegionUVs().buffer();
}

int32_t spine_mesh_attachment_get_num_uvs(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return 0;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return (int32_t) _attachment->getUVs().size();
}

float *spine_mesh_attachment_get_uvs(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return 0;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return _attachment->getUVs().buffer();
}

int32_t spine_mesh_attachment_get_num_triangles(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return 0;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return (int32_t) _attachment->getTriangles().size();
}

uint16_t *spine_mesh_attachment_get_triangles(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return _attachment->getTriangles().buffer();
}

spine_color spine_mesh_attachment_get_color(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return (spine_color) &NULL_COLOR;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return (spine_color) &_attachment->getColor();
}

void spine_mesh_attachment_set_color(spine_mesh_attachment attachment, float r, float g, float b, float a) {
	if (attachment == nullptr) return;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	_attachment->getColor().set(r, g, b, a);
}

const utf8 *spine_mesh_attachment_get_path(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return (utf8 *) _attachment->getPath().buffer();
}

spine_texture_region spine_mesh_attachment_get_region(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return (spine_texture_region) _attachment->getRegion();
}

spine_sequence spine_mesh_attachment_get_sequence(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return (spine_sequence) _attachment->getSequence();
}

spine_mesh_attachment spine_mesh_attachment_get_parent_mesh(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return (spine_mesh_attachment) _attachment->getParentMesh();
}

void spine_mesh_attachment_set_parent_mesh(spine_mesh_attachment attachment, spine_mesh_attachment parentMesh) {
	if (attachment == nullptr) return;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	_attachment->setParentMesh((MeshAttachment *) parentMesh);
}

int32_t spine_mesh_attachment_get_num_edges(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return 0;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return (int32_t) _attachment->getEdges().size();
}

unsigned short *spine_mesh_attachment_get_edges(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return _attachment->getEdges().buffer();
}

float spine_mesh_attachment_get_width(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return 0;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return _attachment->getWidth();
}

void spine_mesh_attachment_set_width(spine_mesh_attachment attachment, float width) {
	if (attachment == nullptr) return;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	_attachment->setWidth(width);
}

float spine_mesh_attachment_get_height(spine_mesh_attachment attachment) {
	if (attachment == nullptr) return 0;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	return _attachment->getHeight();
}

void spine_mesh_attachment_set_height(spine_mesh_attachment attachment, float height) {
	if (attachment == nullptr) return;
	MeshAttachment *_attachment = (MeshAttachment *) attachment;
	_attachment->setHeight(height);
}

// ClippingAttachment
spine_slot_data spine_clipping_attachment_get_end_slot(spine_clipping_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	ClippingAttachment *_attachment = (ClippingAttachment *) attachment;
	return (spine_slot_data) _attachment->getEndSlot();
}

void spine_clipping_attachment_set_end_slot(spine_clipping_attachment attachment, spine_slot_data endSlot) {
	if (attachment == nullptr) return;
	ClippingAttachment *_attachment = (ClippingAttachment *) attachment;
	_attachment->setEndSlot((SlotData *) endSlot);
}

spine_color spine_clipping_attachment_get_color(spine_clipping_attachment attachment) {
	if (attachment == nullptr) return (spine_color) &NULL_COLOR;
	ClippingAttachment *_attachment = (ClippingAttachment *) attachment;
	return (spine_color) &_attachment->getColor();
}

void spine_clipping_attachment_set_color(spine_clipping_attachment attachment, float r, float g, float b, float a) {
	if (attachment == nullptr) return;
	ClippingAttachment *_attachment = (ClippingAttachment *) attachment;
	_attachment->getColor().set(r, g, b, a);
}

// BoundingBoxAttachment
spine_color spine_bounding_box_attachment_get_color(spine_bounding_box_attachment attachment) {
	if (attachment == nullptr) return (spine_color) &NULL_COLOR;
	BoundingBoxAttachment *_attachment = (BoundingBoxAttachment *) attachment;
	return (spine_color) &_attachment->getColor();
}

void spine_bounding_box_attachment_set_color(spine_bounding_box_attachment attachment, float r, float g, float b, float a) {
	if (attachment == nullptr) return;
	BoundingBoxAttachment *_attachment = (BoundingBoxAttachment *) attachment;
	_attachment->getColor().set(r, g, b, a);
}

// PathAttachment
int32_t spine_path_attachment_get_num_lengths(spine_path_attachment attachment) {
	if (attachment == nullptr) return 0;
	PathAttachment *_attachment = (PathAttachment *) attachment;
	return (int32_t) _attachment->getLengths().size();
}

float *spine_path_attachment_get_lengths(spine_path_attachment attachment) {
	if (attachment == nullptr) return nullptr;
	PathAttachment *_attachment = (PathAttachment *) attachment;
	return _attachment->getLengths().buffer();
}

int32_t spine_path_attachment_get_is_closed(spine_path_attachment attachment) {
	if (attachment == nullptr) return 0;
	PathAttachment *_attachment = (PathAttachment *) attachment;
	return _attachment->isClosed() ? -1 : 0;
}

void spine_path_attachment_set_is_closed(spine_path_attachment attachment, int32_t isClosed) {
	if (attachment == nullptr) return;
	PathAttachment *_attachment = (PathAttachment *) attachment;
	_attachment->setClosed(isClosed);
}

int32_t spine_path_attachment_get_is_constant_speed(spine_path_attachment attachment) {
	if (attachment == nullptr) return 0;
	PathAttachment *_attachment = (PathAttachment *) attachment;
	return _attachment->isConstantSpeed() ? -1 : 0;
}

void spine_path_attachment_set_is_constant_speed(spine_path_attachment attachment, int32_t isConstantSpeed) {
	if (attachment == nullptr) return;
	PathAttachment *_attachment = (PathAttachment *) attachment;
	_attachment->setConstantSpeed(isConstantSpeed);
}

spine_color spine_path_attachment_get_color(spine_path_attachment attachment) {
	if (attachment == nullptr) return (spine_color) &NULL_COLOR;
	PathAttachment *_attachment = (PathAttachment *) attachment;
	return (spine_color) &_attachment->getColor();
}

void spine_path_attachment_set_color(spine_path_attachment attachment, float r, float g, float b, float a) {
	if (attachment == nullptr) return;
	PathAttachment *_attachment = (PathAttachment *) attachment;
	_attachment->getColor().set(r, g, b, a);
}

// Skin
void spine_skin_set_attachment(spine_skin skin, int32_t slotIndex, const utf8 *name, spine_attachment attachment) {
	if (skin == nullptr) return;
	Skin *_skin = (Skin *) skin;
	_skin->setAttachment(slotIndex, name, (Attachment *) attachment);
}

spine_attachment spine_skin_get_attachment(spine_skin skin, int32_t slotIndex, const utf8 *name) {
	if (skin == nullptr) return nullptr;
	Skin *_skin = (Skin *) skin;
	return (spine_attachment) _skin->getAttachment(slotIndex, name);
}

void spine_skin_remove_attachment(spine_skin skin, int32_t slotIndex, const utf8 *name) {
	if (skin == nullptr) return;
	Skin *_skin = (Skin *) skin;
	_skin->removeAttachment(slotIndex, name);
}

const utf8 *spine_skin_get_name(spine_skin skin) {
	if (skin == nullptr) return nullptr;
	Skin *_skin = (Skin *) skin;
	return (utf8 *) _skin->getName().buffer();
}

void spine_skin_add_skin(spine_skin skin, spine_skin other) {
	if (skin == nullptr) return;
	if (other == nullptr) return;
	Skin *_skin = (Skin *) skin;
	_skin->addSkin((Skin *) other);
}

void spine_skin_copy_skin(spine_skin skin, spine_skin other) {
	if (skin == nullptr) return;
	if (other == nullptr) return;
	Skin *_skin = (Skin *) skin;
	_skin->copySkin((Skin *) other);
}

spine_skin_entries spine_skin_get_entries(spine_skin skin) {
	if (skin == nullptr) return nullptr;
	Skin *_skin = (Skin *) skin;
	_spine_skin_entries *entries = SpineExtension::getInstance()->calloc<_spine_skin_entries>(1, __FILE__, __LINE__);
	{
		Skin::AttachmentMap::Entries mapEntries = _skin->getAttachments();
		while (mapEntries.hasNext()) entries->numEntries++;
	}
	{
		entries->entries = SpineExtension::getInstance()->calloc<_spine_skin_entry>(entries->numEntries, __FILE__, __LINE__);
		Skin::AttachmentMap::Entries mapEntries = _skin->getAttachments();
		int32_t i = 0;
		while (mapEntries.hasNext()) {
			Skin::AttachmentMap::Entry entry = mapEntries.next();
			entries->entries[i++] = {(int32_t) entry._slotIndex, (utf8 *) entry._name.buffer(), (spine_attachment) entry._attachment};
		}
	}
	return (spine_skin_entries) entries;
}

int32_t spine_skin_entries_get_num_entries(spine_skin_entries entries) {
	if (!entries) return 0;
	return ((_spine_skin_entries *) entries)->numEntries;
}

spine_skin_entry spine_skin_entries_get_entry(spine_skin_entries entries, int32_t index) {
	if (!entries) return 0;
	return (spine_skin_entry) & ((_spine_skin_entries *) entries)->entries[index];
}

void spine_skin_entries_dispose(spine_skin_entries entries) {
	if (entries == nullptr) return;
	SpineExtension::getInstance()->free(((_spine_skin_entries *) entries)->entries, __FILE__, __LINE__);
	SpineExtension::getInstance()->free(entries, __FILE__, __LINE__);
}

int32_t spine_skin_entry_get_slot_index(spine_skin_entry entry) {
	if (!entry) return 0;
	return ((_spine_skin_entry *) entry)->slotIndex;
}

utf8 *spine_skin_entry_get_name(spine_skin_entry entry) {
	if (!entry) return nullptr;
	return ((_spine_skin_entry *) entry)->name;
}

spine_attachment spine_skin_entry_get_attachment(spine_skin_entry entry) {
	if (!entry) return nullptr;
	return ((_spine_skin_entry *) entry)->attachment;
}

int32_t spine_skin_get_num_bones(spine_skin skin) {
	if (skin == nullptr) return 0;
	Skin *_skin = (Skin *) skin;
	return (int32_t) _skin->getBones().size();
}

spine_bone_data *spine_skin_get_bones(spine_skin skin) {
	if (skin == nullptr) return nullptr;
	Skin *_skin = (Skin *) skin;
	return (spine_bone_data *) _skin->getBones().buffer();
}

int32_t spine_skin_get_num_constraints(spine_skin skin) {
	if (skin == nullptr) return 0;
	Skin *_skin = (Skin *) skin;
	return (int32_t) _skin->getConstraints().size();
}

spine_constraint_data *spine_skin_get_constraints(spine_skin skin) {
	if (skin == nullptr) return nullptr;
	Skin *_skin = (Skin *) skin;
	return (spine_constraint_data *) _skin->getConstraints().buffer();
}

spine_skin spine_skin_create(const utf8 *name) {
	if (name == nullptr) return nullptr;
	return (spine_skin) new (__FILE__, __LINE__) Skin(name);
}

void spine_skin_dispose(spine_skin skin) {
	if (skin == nullptr) return;
	Skin *_skin = (Skin *) skin;
	delete _skin;
}

// ConstraintData
spine_constraint_type spine_constraint_data_get_type(spine_constraint_data data) {
	if (data == nullptr) return SPINE_CONSTRAINT_IK;
	ConstraintData *_data = (ConstraintData *) data;
	if (_data->getRTTI().isExactly(IkConstraintData::rtti)) {
		return SPINE_CONSTRAINT_IK;
	} else if (_data->getRTTI().isExactly(TransformConstraintData::rtti)) {
		return SPINE_CONSTRAINT_TRANSFORM;
	} else if (_data->getRTTI().isExactly(PathConstraintData::rtti)) {
		return SPINE_CONSTRAINT_PATH;
	} else {
		return SPINE_CONSTRAINT_IK;
	}
}

const utf8 *spine_constraint_data_get_name(spine_constraint_data data) {
	if (data == nullptr) return nullptr;
	ConstraintData *_data = (ConstraintData *) data;
	return (utf8 *) _data->getName().buffer();
}

uint64_t spine_constraint_data_get_order(spine_constraint_data data) {
	if (data == nullptr) return 0;
	ConstraintData *_data = (ConstraintData *) data;
	return (uint64_t) _data->getOrder();
}

void spine_constraint_data_set_order(spine_constraint_data data, uint64_t order) {
	if (data == nullptr) return;
	ConstraintData *_data = (ConstraintData *) data;
	_data->setOrder((size_t) order);
}

int32_t spine_constraint_data_get_is_skin_required(spine_constraint_data data) {
	if (data == nullptr) return 0;
	ConstraintData *_data = (ConstraintData *) data;
	return _data->isSkinRequired() ? -1 : 0;
}

void spine_constraint_data_set_is_skin_required(spine_constraint_data data, int32_t isSkinRequired) {
	if (data == nullptr) return;
	ConstraintData *_data = (ConstraintData *) data;
	_data->setSkinRequired(isSkinRequired);
}

// IkConstraintData
int32_t spine_ik_constraint_data_get_num_bones(spine_ik_constraint_data data) {
	if (data == nullptr) return 0;
	IkConstraintData *_data = (IkConstraintData *) data;
	return (int32_t) _data->getBones().size();
}

spine_bone_data *spine_ik_constraint_data_get_bones(spine_ik_constraint_data data) {
	if (data == nullptr) return nullptr;
	IkConstraintData *_data = (IkConstraintData *) data;
	return (spine_bone_data *) _data->getBones().buffer();
}

spine_bone_data spine_ik_constraint_data_get_target(spine_ik_constraint_data data) {
	if (data == nullptr) return nullptr;
	IkConstraintData *_data = (IkConstraintData *) data;
	return (spine_bone_data) _data->getTarget();
}

void spine_ik_constraint_data_set_target(spine_ik_constraint_data data, spine_bone_data target) {
	if (data == nullptr) return;
	IkConstraintData *_data = (IkConstraintData *) data;
	_data->setTarget((BoneData *) target);
}

int32_t spine_ik_constraint_data_get_bend_direction(spine_ik_constraint_data data) {
	if (data == nullptr) return 1;
	IkConstraintData *_data = (IkConstraintData *) data;
	return _data->getBendDirection();
}

void spine_ik_constraint_data_set_bend_direction(spine_ik_constraint_data data, int32_t bendDirection) {
	if (data == nullptr) return;
	IkConstraintData *_data = (IkConstraintData *) data;
	_data->setBendDirection(bendDirection);
}

int32_t spine_ik_constraint_data_get_compress(spine_ik_constraint_data data) {
	if (data == nullptr) return 0;
	IkConstraintData *_data = (IkConstraintData *) data;
	return _data->getCompress() ? -1 : 0;
}

void spine_ik_constraint_data_set_compress(spine_ik_constraint_data data, int32_t compress) {
	if (data == nullptr) return;
	IkConstraintData *_data = (IkConstraintData *) data;
	_data->setCompress(compress);
}

int32_t spine_ik_constraint_data_get_stretch(spine_ik_constraint_data data) {
	if (data == nullptr) return 0;
	IkConstraintData *_data = (IkConstraintData *) data;
	return _data->getStretch() ? -1 : 0;
}

void spine_ik_constraint_data_set_stretch(spine_ik_constraint_data data, int32_t stretch) {
	if (data == nullptr) return;
	IkConstraintData *_data = (IkConstraintData *) data;
	_data->setStretch(stretch);
}

int32_t spine_ik_constraint_data_get_uniform(spine_ik_constraint_data data) {
	if (data == nullptr) return 0;
	IkConstraintData *_data = (IkConstraintData *) data;
	return _data->getUniform() ? -1 : 0;
}

float spine_ik_constraint_data_get_mix(spine_ik_constraint_data data) {
	if (data == nullptr) return 0;
	IkConstraintData *_data = (IkConstraintData *) data;
	return _data->getMix();
}

void spine_ik_constraint_data_set_mix(spine_ik_constraint_data data, float mix) {
	if (data == nullptr) return;
	IkConstraintData *_data = (IkConstraintData *) data;
	_data->setMix(mix);
}

float spine_ik_constraint_data_get_softness(spine_ik_constraint_data data) {
	if (data == nullptr) return 0;
	IkConstraintData *_data = (IkConstraintData *) data;
	return _data->getSoftness();
}

void spine_ik_constraint_data_set_softness(spine_ik_constraint_data data, float softness) {
	if (data == nullptr) return;
	IkConstraintData *_data = (IkConstraintData *) data;
	_data->setSoftness(softness);
}

// IKConstraint
void spine_ik_constraint_update(spine_ik_constraint constraint) {
	if (constraint == nullptr) return;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	_constraint->update();
}

int32_t spine_ik_constraint_get_order(spine_ik_constraint constraint) {
	if (constraint == nullptr) return 0;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	return _constraint->getOrder();
}

spine_ik_constraint_data spine_ik_constraint_get_data(spine_ik_constraint constraint) {
	if (constraint == nullptr) return nullptr;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	return (spine_ik_constraint_data) &_constraint->getData();
}

int32_t spine_ik_constraint_get_num_bones(spine_ik_constraint constraint) {
	if (constraint == nullptr) return 0;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	return (int32_t) _constraint->getBones().size();
}

spine_bone *spine_ik_constraint_get_bones(spine_ik_constraint constraint) {
	if (constraint == nullptr) return nullptr;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	return (spine_bone *) _constraint->getBones().buffer();
}

spine_bone spine_ik_constraint_get_target(spine_ik_constraint constraint) {
	if (constraint == nullptr) return nullptr;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	return (spine_bone) _constraint->getTarget();
}

void spine_ik_constraint_set_target(spine_ik_constraint constraint, spine_bone target) {
	if (constraint == nullptr) return;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	_constraint->setTarget((Bone *) target);
}

int32_t spine_ik_constraint_get_bend_direction(spine_ik_constraint constraint) {
	if (constraint == nullptr) return 1;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	return _constraint->getBendDirection();
}

void spine_ik_constraint_set_bend_direction(spine_ik_constraint constraint, int32_t bendDirection) {
	if (constraint == nullptr) return;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	_constraint->setBendDirection(bendDirection);
}

int32_t spine_ik_constraint_get_compress(spine_ik_constraint constraint) {
	if (constraint == nullptr) return 0;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	return _constraint->getCompress() ? -1 : 0;
}

void spine_ik_constraint_set_compress(spine_ik_constraint constraint, int32_t compress) {
	if (constraint == nullptr) return;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	_constraint->setCompress(compress);
}

int32_t spine_ik_constraint_get_stretch(spine_ik_constraint constraint) {
	if (constraint == nullptr) return 0;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	return _constraint->getStretch() ? -1 : 0;
}

void spine_ik_constraint_set_stretch(spine_ik_constraint constraint, int32_t stretch) {
	if (constraint == nullptr) return;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	_constraint->setStretch(stretch);
}

float spine_ik_constraint_get_mix(spine_ik_constraint constraint) {
	if (constraint == nullptr) return 0;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	return _constraint->getMix();
}

void spine_ik_constraint_set_mix(spine_ik_constraint constraint, float mix) {
	if (constraint == nullptr) return;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	_constraint->setMix(mix);
}

float spine_ik_constraint_get_softness(spine_ik_constraint constraint) {
	if (constraint == nullptr) return 0;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	return _constraint->getSoftness();
}

void spine_ik_constraint_set_softness(spine_ik_constraint constraint, float softness) {
	if (constraint == nullptr) return;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	_constraint->setSoftness(softness);
}

int32_t spine_ik_constraint_get_is_active(spine_ik_constraint constraint) {
	if (constraint == nullptr) return 0;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	return _constraint->isActive() ? -1 : 0;
}

void spine_ik_constraint_set_is_active(spine_ik_constraint constraint, int32_t isActive) {
	if (constraint == nullptr) return;
	IkConstraint *_constraint = (IkConstraint *) constraint;
	_constraint->setActive(isActive);
}

// TransformConstraintData
int32_t spine_transform_constraint_data_get_num_bones(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return (int32_t) _data->getBones().size();
}

spine_bone_data *spine_transform_constraint_data_get_bones(spine_transform_constraint_data data) {
	if (data == nullptr) return nullptr;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return (spine_bone_data *) _data->getBones().buffer();
}

spine_bone_data spine_transform_constraint_data_get_target(spine_transform_constraint_data data) {
	if (data == nullptr) return nullptr;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return (spine_bone_data) _data->getTarget();
}

void spine_transform_constraint_data_set_target(spine_transform_constraint_data data, spine_bone_data target) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setTarget((BoneData *) target);
}

float spine_transform_constraint_data_get_mix_rotate(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getMixRotate();
}

void spine_transform_constraint_data_set_mix_rotate(spine_transform_constraint_data data, float mixRotate) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setMixRotate(mixRotate);
}

float spine_transform_constraint_data_get_mix_x(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getMixX();
}

void spine_transform_constraint_data_set_mix_x(spine_transform_constraint_data data, float mixX) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setMixX(mixX);
}

float spine_transform_constraint_data_get_mix_y(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getMixY();
}

void spine_transform_constraint_data_set_mix_y(spine_transform_constraint_data data, float mixY) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setMixY(mixY);
}

float spine_transform_constraint_data_get_mix_scale_x(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getMixScaleX();
}

void spine_transform_constraint_data_set_mix_scale_x(spine_transform_constraint_data data, float mixScaleX) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setMixScaleX(mixScaleX);
}

float spine_transform_constraint_data_get_mix_scale_y(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getMixScaleY();
}

void spine_transform_constraint_data_set_mix_scale_y(spine_transform_constraint_data data, float mixScaleY) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setMixScaleY(mixScaleY);
}

float spine_transform_constraint_data_get_mix_shear_y(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getMixShearY();
}

void spine_transform_constraint_data_set_mix_shear_y(spine_transform_constraint_data data, float mixShearY) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setMixShearY(mixShearY);
}

float spine_transform_constraint_data_get_offset_rotation(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getOffsetRotation();
}

void spine_transform_constraint_data_set_offset_rotation(spine_transform_constraint_data data, float offsetRotation) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setOffsetRotation(offsetRotation);
}

float spine_transform_constraint_data_get_offset_x(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getOffsetX();
}

void spine_transform_constraint_data_set_offset_x(spine_transform_constraint_data data, float offsetX) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setOffsetX(offsetX);
}

float spine_transform_constraint_data_get_offset_y(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getOffsetY();
}

void spine_transform_constraint_data_set_offset_y(spine_transform_constraint_data data, float offsetY) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setOffsetY(offsetY);
}

float spine_transform_constraint_data_get_offset_scale_x(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getOffsetScaleX();
}

void spine_transform_constraint_data_set_offset_scale_x(spine_transform_constraint_data data, float offsetScaleX) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setOffsetScaleX(offsetScaleX);
}

float spine_transform_constraint_data_get_offset_scale_y(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getOffsetScaleY();
}

void spine_transform_constraint_data_set_offset_scale_y(spine_transform_constraint_data data, float offsetScaleY) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setOffsetScaleY(offsetScaleY);
}

float spine_transform_constraint_data_get_offset_shear_y(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->getOffsetShearY();
}

void spine_transform_constraint_data_set_offset_shear_y(spine_transform_constraint_data data, float offsetShearY) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setOffsetShearY(offsetShearY);
}

int32_t spine_transform_constraint_data_get_is_relative(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->isRelative() ? -1 : 0;
}

void spine_transform_constraint_data_set_is_relative(spine_transform_constraint_data data, int32_t isRelative) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setRelative(isRelative);
}

int32_t spine_transform_constraint_data_get_is_local(spine_transform_constraint_data data) {
	if (data == nullptr) return 0;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	return _data->isLocal() ? -1 : 0;
}

void spine_transform_constraint_data_set_is_local(spine_transform_constraint_data data, int32_t isLocal) {
	if (data == nullptr) return;
	TransformConstraintData *_data = (TransformConstraintData *) data;
	_data->setLocal(isLocal);
}

// TransformConstraint
void spine_transform_constraint_update(spine_transform_constraint constraint) {
	if (constraint == nullptr) return;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	_constraint->update();
}

int32_t spine_transform_constraint_get_order(spine_transform_constraint constraint) {
	if (constraint == nullptr) return 0;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return _constraint->getOrder();
}

spine_transform_constraint_data spine_transform_constraint_get_data(spine_transform_constraint constraint) {
	if (constraint == nullptr) return nullptr;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return (spine_transform_constraint_data) &_constraint->getData();
}

int32_t spine_transform_constraint_get_num_bones(spine_transform_constraint constraint) {
	if (constraint == nullptr) return 0;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return (int32_t) _constraint->getBones().size();
}

spine_bone *spine_transform_constraint_get_bones(spine_transform_constraint constraint) {
	if (constraint == nullptr) return nullptr;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return (spine_bone *) _constraint->getBones().buffer();
}

spine_bone spine_transform_constraint_get_target(spine_transform_constraint constraint) {
	if (constraint == nullptr) return nullptr;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return (spine_bone) _constraint->getTarget();
}

void spine_transform_constraint_set_target(spine_transform_constraint constraint, spine_bone target) {
	if (constraint == nullptr) return;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	_constraint->setTarget((Bone *) target);
}

float spine_transform_constraint_get_mix_rotate(spine_transform_constraint constraint) {
	if (constraint == nullptr) return 0;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return _constraint->getMixRotate();
}

void spine_transform_constraint_set_mix_rotate(spine_transform_constraint constraint, float mixRotate) {
	if (constraint == nullptr) return;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	_constraint->setMixRotate(mixRotate);
}

float spine_transform_constraint_get_mix_x(spine_transform_constraint constraint) {
	if (constraint == nullptr) return 0;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return _constraint->getMixX();
}

void spine_transform_constraint_set_mix_x(spine_transform_constraint constraint, float mixX) {
	if (constraint == nullptr) return;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	_constraint->setMixX(mixX);
}

float spine_transform_constraint_get_mix_y(spine_transform_constraint constraint) {
	if (constraint == nullptr) return 0;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return _constraint->getMixY();
}

void spine_transform_constraint_set_mix_y(spine_transform_constraint constraint, float mixY) {
	if (constraint == nullptr) return;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	_constraint->setMixY(mixY);
}

float spine_transform_constraint_get_mix_scale_x(spine_transform_constraint constraint) {
	if (constraint == nullptr) return 0;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return _constraint->getMixScaleX();
}

void spine_transform_constraint_set_mix_scale_x(spine_transform_constraint constraint, float mixScaleX) {
	if (constraint == nullptr) return;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	_constraint->setMixScaleX(mixScaleX);
}

float spine_transform_constraint_get_mix_scale_y(spine_transform_constraint constraint) {
	if (constraint == nullptr) return 0;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return _constraint->getMixScaleY();
}

void spine_transform_constraint_set_mix_scale_y(spine_transform_constraint constraint, float mixScaleY) {
	if (constraint == nullptr) return;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	_constraint->setMixScaleY(mixScaleY);
}

float spine_transform_constraint_get_mix_shear_y(spine_transform_constraint constraint) {
	if (constraint == nullptr) return 0;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return _constraint->getMixShearY();
}

void spine_transform_constraint_set_mix_shear_y(spine_transform_constraint constraint, float mixShearY) {
	if (constraint == nullptr) return;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	_constraint->setMixShearY(mixShearY);
}

float spine_transform_constraint_get_is_active(spine_transform_constraint constraint) {
	if (constraint == nullptr) return 0;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	return _constraint->isActive() ? -1 : 0;
}

void spine_transform_constraint_set_is_active(spine_transform_constraint constraint, int32_t isActive) {
	if (constraint == nullptr) return;
	TransformConstraint *_constraint = (TransformConstraint *) constraint;
	_constraint->setActive(isActive);
}

// PathConstraintData
int32_t spine_path_constraint_data_get_num_bones(spine_path_constraint_data data) {
	if (data == nullptr) return 0;
	PathConstraintData *_data = (PathConstraintData *) data;
	return (int32_t) _data->getBones().size();
}

spine_bone_data *spine_path_constraint_data_get_bones(spine_path_constraint_data data) {
	if (data == nullptr) return nullptr;
	PathConstraintData *_data = (PathConstraintData *) data;
	return (spine_bone_data *) _data->getBones().buffer();
}

spine_slot_data spine_path_constraint_data_get_target(spine_path_constraint_data data) {
	if (data == nullptr) return nullptr;
	PathConstraintData *_data = (PathConstraintData *) data;
	return (spine_slot_data) _data->getTarget();
}

void spine_path_constraint_data_set_target(spine_path_constraint_data data, spine_slot_data target) {
	if (data == nullptr) return;
	PathConstraintData *_data = (PathConstraintData *) data;
	_data->setTarget((SlotData *) target);
}

spine_position_mode spine_path_constraint_data_get_position_mode(spine_path_constraint_data data) {
	if (data == nullptr) return SPINE_POSITION_MODE_FIXED;
	PathConstraintData *_data = (PathConstraintData *) data;
	return (spine_position_mode) _data->getPositionMode();
}

void spine_path_constraint_data_set_position_mode(spine_path_constraint_data data, spine_position_mode positionMode) {
	if (data == nullptr) return;
	PathConstraintData *_data = (PathConstraintData *) data;
	_data->setPositionMode((PositionMode) positionMode);
}

spine_spacing_mode spine_path_constraint_data_get_spacing_mode(spine_path_constraint_data data) {
	if (data == nullptr) return SPINE_SPACING_MODE_LENGTH;
	PathConstraintData *_data = (PathConstraintData *) data;
	return (spine_spacing_mode) _data->getSpacingMode();
}

void spine_path_constraint_data_set_spacing_mode(spine_path_constraint_data data, spine_spacing_mode spacingMode) {
	if (data == nullptr) return;
	PathConstraintData *_data = (PathConstraintData *) data;
	_data->setSpacingMode((SpacingMode) spacingMode);
}

spine_rotate_mode spine_path_constraint_data_get_rotate_mode(spine_path_constraint_data data) {
	if (data == nullptr) return SPINE_ROTATE_MODE_TANGENT;
	PathConstraintData *_data = (PathConstraintData *) data;
	return (spine_rotate_mode) _data->getRotateMode();
}

void spine_path_constraint_data_set_rotate_mode(spine_path_constraint_data data, spine_rotate_mode rotateMode) {
	if (data == nullptr) return;
	PathConstraintData *_data = (PathConstraintData *) data;
	_data->setRotateMode((RotateMode) rotateMode);
}

float spine_path_constraint_data_get_offset_rotation(spine_path_constraint_data data) {
	if (data == nullptr) return 0;
	PathConstraintData *_data = (PathConstraintData *) data;
	return _data->getOffsetRotation();
}

void spine_path_constraint_data_set_offset_rotation(spine_path_constraint_data data, float offsetRotation) {
	if (data == nullptr) return;
	PathConstraintData *_data = (PathConstraintData *) data;
	_data->setOffsetRotation(offsetRotation);
}

float spine_path_constraint_data_get_position(spine_path_constraint_data data) {
	if (data == nullptr) return 0;
	PathConstraintData *_data = (PathConstraintData *) data;
	return _data->getPosition();
}

void spine_path_constraint_data_set_position(spine_path_constraint_data data, float position) {
	if (data == nullptr) return;
	PathConstraintData *_data = (PathConstraintData *) data;
	_data->setPosition(position);
}

float spine_path_constraint_data_get_spacing(spine_path_constraint_data data) {
	if (data == nullptr) return 0;
	PathConstraintData *_data = (PathConstraintData *) data;
	return _data->getSpacing();
}

void spine_path_constraint_data_set_spacing(spine_path_constraint_data data, float spacing) {
	if (data == nullptr) return;
	PathConstraintData *_data = (PathConstraintData *) data;
	_data->setSpacing(spacing);
}

float spine_path_constraint_data_get_mix_rotate(spine_path_constraint_data data) {
	if (data == nullptr) return 0;
	PathConstraintData *_data = (PathConstraintData *) data;
	return _data->getMixRotate();
}

void spine_path_constraint_data_set_mix_rotate(spine_path_constraint_data data, float mixRotate) {
	if (data == nullptr) return;
	PathConstraintData *_data = (PathConstraintData *) data;
	_data->setMixRotate(mixRotate);
}

float spine_path_constraint_data_get_mix_x(spine_path_constraint_data data) {
	if (data == nullptr) return 0;
	PathConstraintData *_data = (PathConstraintData *) data;
	return _data->getMixX();
}

void spine_path_constraint_data_set_mix_x(spine_path_constraint_data data, float mixX) {
	if (data == nullptr) return;
	PathConstraintData *_data = (PathConstraintData *) data;
	_data->setMixX(mixX);
}

float spine_path_constraint_data_get_mix_y(spine_path_constraint_data data) {
	if (data == nullptr) return 0;
	PathConstraintData *_data = (PathConstraintData *) data;
	return _data->getMixY();
}

void spine_path_constraint_data_set_mix_y(spine_path_constraint_data data, float mixY) {
	if (data == nullptr) return;
	PathConstraintData *_data = (PathConstraintData *) data;
	_data->setMixY(mixY);
}

// PathConstraint
void spine_path_constraint_update(spine_path_constraint constraint) {
	if (constraint == nullptr) return;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	_constraint->update();
}

int32_t spine_path_constraint_get_order(spine_path_constraint constraint) {
	if (constraint == nullptr) return 0;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	return _constraint->getOrder();
}

spine_path_constraint_data spine_path_constraint_get_data(spine_path_constraint constraint) {
	if (constraint == nullptr) return nullptr;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	return (spine_path_constraint_data) &_constraint->getData();
}

int32_t spine_path_constraint_get_num_bones(spine_path_constraint constraint) {
	if (constraint == nullptr) return 0;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	return (int32_t) _constraint->getBones().size();
}

spine_bone *spine_path_constraint_get_bones(spine_path_constraint constraint) {
	if (constraint == nullptr) return nullptr;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	return (spine_bone *) _constraint->getBones().buffer();
}

spine_slot spine_path_constraint_get_target(spine_path_constraint constraint) {
	if (constraint == nullptr) return nullptr;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	return (spine_slot) _constraint->getTarget();
}

void spine_path_constraint_set_target(spine_path_constraint constraint, spine_slot target) {
	if (constraint == nullptr) return;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	_constraint->setTarget((Slot *) target);
}

float spine_path_constraint_get_position(spine_path_constraint constraint) {
	if (constraint == nullptr) return 0;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	return _constraint->getPosition();
}

void spine_path_constraint_set_position(spine_path_constraint constraint, float position) {
	if (constraint == nullptr) return;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	_constraint->setPosition(position);
}

float spine_path_constraint_get_spacing(spine_path_constraint constraint) {
	if (constraint == nullptr) return 0;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	return _constraint->getSpacing();
}

void spine_path_constraint_set_spacing(spine_path_constraint constraint, float spacing) {
	if (constraint == nullptr) return;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	_constraint->setSpacing(spacing);
}

float spine_path_constraint_get_mix_rotate(spine_path_constraint constraint) {
	if (constraint == nullptr) return 0;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	return _constraint->getMixRotate();
}

void spine_path_constraint_set_mix_rotate(spine_path_constraint constraint, float mixRotate) {
	if (constraint == nullptr) return;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	_constraint->setMixRotate(mixRotate);
}

float spine_path_constraint_get_mix_x(spine_path_constraint constraint) {
	if (constraint == nullptr) return 0;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	return _constraint->getMixX();
}

void spine_path_constraint_set_mix_x(spine_path_constraint constraint, float mixX) {
	if (constraint == nullptr) return;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	_constraint->setMixX(mixX);
}

float spine_path_constraint_get_mix_y(spine_path_constraint constraint) {
	if (constraint == nullptr) return 0;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	return _constraint->getMixY();
}

void spine_path_constraint_set_mix_y(spine_path_constraint constraint, float mixY) {
	if (constraint == nullptr) return;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	_constraint->setMixY(mixY);
}

int32_t spine_path_constraint_get_is_active(spine_path_constraint constraint) {
	if (constraint == nullptr) return 0;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	return _constraint->isActive() ? -1 : 0;
}

void spine_path_constraint_set_is_active(spine_path_constraint constraint, int32_t isActive) {
	if (constraint == nullptr) return;
	PathConstraint *_constraint = (PathConstraint *) constraint;
	_constraint->setActive(isActive);
}

// Sequence
void spine_sequence_apply(spine_sequence sequence, spine_slot slot, spine_attachment attachment) {
	if (sequence == nullptr) return;
	Sequence *_sequence = (Sequence *) sequence;
	_sequence->apply((Slot *) slot, (Attachment *) attachment);
}

const utf8 *spine_sequence_get_path(spine_sequence sequence, const utf8 *basePath, int32_t index) {
	if (sequence == nullptr) return nullptr;
	Sequence *_sequence = (Sequence *) sequence;
	return (utf8 *) strdup(_sequence->getPath(basePath, index).buffer());
}

int32_t spine_sequence_get_id(spine_sequence sequence) {
	if (sequence == nullptr) return 0;
	Sequence *_sequence = (Sequence *) sequence;
	return _sequence->getId();
}

void spine_sequence_set_id(spine_sequence sequence, int32_t id) {
	if (sequence == nullptr) return;
	Sequence *_sequence = (Sequence *) sequence;
	_sequence->setId(id);
}

int32_t spine_sequence_get_start(spine_sequence sequence) {
	if (sequence == nullptr) return 0;
	Sequence *_sequence = (Sequence *) sequence;
	return _sequence->getStart();
}

void spine_sequence_set_start(spine_sequence sequence, int32_t start) {
	if (sequence == nullptr) return;
	Sequence *_sequence = (Sequence *) sequence;
	_sequence->setStart(start);
}

int32_t spine_sequence_get_digits(spine_sequence sequence) {
	if (sequence == nullptr) return 0;
	Sequence *_sequence = (Sequence *) sequence;
	return _sequence->getDigits();
}

void spine_sequence_set_digits(spine_sequence sequence, int32_t digits) {
	if (sequence == nullptr) return;
	Sequence *_sequence = (Sequence *) sequence;
	_sequence->setDigits(digits);
}

int32_t spine_sequence_get_setup_index(spine_sequence sequence) {
	if (sequence == nullptr) return 0;
	Sequence *_sequence = (Sequence *) sequence;
	return _sequence->getSetupIndex();
}

void spine_sequence_set_setup_index(spine_sequence sequence, int32_t setupIndex) {
	if (sequence == nullptr) return;
	Sequence *_sequence = (Sequence *) sequence;
	_sequence->setSetupIndex(setupIndex);
}

int32_t spine_sequence_get_num_regions(spine_sequence sequence) {
	if (sequence == nullptr) return 0;
	Sequence *_sequence = (Sequence *) sequence;
	return (int32_t) _sequence->getRegions().size();
}

spine_texture_region *spine_sequence_get_regions(spine_sequence sequence) {
	if (sequence == nullptr) return nullptr;
	Sequence *_sequence = (Sequence *) sequence;
	return (spine_texture_region *) _sequence->getRegions().buffer();
}

// TextureRegion
void *spine_texture_region_get_texture(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return nullptr;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->rendererObject;
}

void spine_texture_region_set_texture(spine_texture_region textureRegion, void *texture) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->rendererObject = texture;
}

float spine_texture_region_get_u(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return 0;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->u;
}

void spine_texture_region_set_u(spine_texture_region textureRegion, float u) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->u = u;
}

float spine_texture_region_get_v(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return 0;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->v;
}
void spine_texture_region_set_v(spine_texture_region textureRegion, float v) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->v = v;
}
float spine_texture_region_get_u2(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return 0;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->u2;
}

void spine_texture_region_set_u2(spine_texture_region textureRegion, float u2) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->u2 = u2;
}

float spine_texture_region_get_v2(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return 0;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->v2;
}

void spine_texture_region_set_v2(spine_texture_region textureRegion, float v2) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->v2 = v2;
}

int32_t spine_texture_region_get_degrees(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return 0;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->degrees;
}

void spine_texture_region_set_degrees(spine_texture_region textureRegion, int32_t degrees) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->degrees = degrees;
}

float spine_texture_region_get_offset_x(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return 0;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->offsetX;
}

void spine_texture_region_set_offset_x(spine_texture_region textureRegion, float offsetX) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->offsetX = offsetX;
}

float spine_texture_region_get_offset_y(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return 0;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->offsetY;
}

void spine_texture_region_set_offset_y(spine_texture_region textureRegion, float offsetY) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->offsetY = offsetY;
}

int32_t spine_texture_region_get_width(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return 0;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->width;
}

void spine_texture_region_set_width(spine_texture_region textureRegion, int32_t width) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->width = width;
}

int32_t spine_texture_region_get_height(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return 0;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->height;
}

void spine_texture_region_set_height(spine_texture_region textureRegion, int32_t height) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->height = height;
}

int32_t spine_texture_region_get_original_width(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return 0;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->originalWidth;
}

void spine_texture_region_set_original_width(spine_texture_region textureRegion, int32_t originalWidth) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->originalWidth = originalWidth;
}

int32_t spine_texture_region_get_original_height(spine_texture_region textureRegion) {
	if (textureRegion == nullptr) return 0;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	return _region->originalHeight;
}

void spine_texture_region_set_original_height(spine_texture_region textureRegion, int32_t originalHeight) {
	if (textureRegion == nullptr) return;
	TextureRegion *_region = (TextureRegion *) textureRegion;
	_region->originalHeight = originalHeight;
}
