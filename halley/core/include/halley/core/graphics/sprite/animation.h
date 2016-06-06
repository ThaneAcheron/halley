#pragma once

#include <halley/text/halleystring.h>
#include <memory>
#include <halley/resources/resource.h>

namespace Halley
{
	class ResourceLoader;
	class SpriteSheet;
	class SpriteSheetEntry;
	class Material;
	class AnimationDirection;

	class AnimationFrame
	{
	public:
		AnimationFrame(int frameNumber, String imageName, SpriteSheet& sheet, const Vector<AnimationDirection>& directions);
		const SpriteSheetEntry& getSprite(int dir) const { return *sprites[dir]; }

	private:
		Vector<const SpriteSheetEntry*> sprites;
	};

	class AnimationSequence
	{
		friend class Animation;
	public:
		float getFPS() const { return fps; }
		size_t numFrames() const { return frames.size(); }
		const AnimationFrame& getFrame(size_t n) const { return frames[n]; }
		String getName() const { return name; }
		bool isLooping() const { return loop; }
		bool isNoFlip() const { return noFlip; }

	private:
		Vector<AnimationFrame> frames;
		String name;
		float fps;
		bool loop;
		bool noFlip;
	};

	class AnimationDirection
	{
		friend class Animation;

	public:
		AnimationDirection(String name, String fileName, bool flip, int id);
		String getName() const { return name; }
		String getFileName() const { return fileName; }
		bool shouldFlip() const { return flip; }
		int getId() const { return id; }
		String getFrameName(int frameNumber, String baseName) const;

	private:
		String name;
		String fileName;
		int id;
		bool flip;
	};
	
	class Animation final : public Resource
	{
	public:
		static std::unique_ptr<Animation> loadResource(ResourceLoader& loader);
		SpriteSheet& getSpriteSheet() const { return *spriteSheet; }
		std::shared_ptr<Material> getMaterial() const { return material; }
		const AnimationSequence& getSequence(String name) const;
		const AnimationDirection& getDirection(String name) const;
		const AnimationDirection& getDirection(int id) const;

	private:
		explicit Animation(ResourceLoader& loader);

		String name;
		std::shared_ptr<SpriteSheet> spriteSheet;
		std::shared_ptr<Material> material;
		Vector<AnimationSequence> sequences;
		Vector<AnimationDirection> directions;
	};
}
