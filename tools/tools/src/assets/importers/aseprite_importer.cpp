#include "aseprite_importer.h"
#include "halley/tools/assets/import_assets_database.h"
#include "halley/file/byte_serializer.h"
#include "halley/resources/metadata.h"
#include "halley/file_formats/image.h"
#include "halley/tools/file/filesystem.h"
#include "halley/core/graphics/sprite/sprite_sheet.h"
#include "halley/core/graphics/sprite/animation.h"
#include <sstream>
#include "halley/data_structures/bin_pack.h"

using namespace Halley;

std::vector<Path> AsepriteImporter::import(const ImportingAsset& asset, Path dstDir, ProgressReporter reporter, AssetCollector collector)
{
	Path srcPath = asset.inputFiles.at(0).name;
	String baseName = srcPath.getStem().getString();
	Path basePath = srcPath.parentPath().dropFront(1) / baseName;
	Path animationPath = Path("animation") / basePath;
	Path spriteSheetPath = Path("spritesheet") / basePath;
	Path imagePath = (Path("image") / basePath).replaceExtension(".png");
	Path imageMetaPath = imagePath.replaceExtension(imagePath.getExtension() + ".meta");

	// Meta
	Metadata meta;
	if (asset.metadata) {
		meta = *asset.metadata;
	}

	// Import
	auto frames = importAseprite(baseName, gsl::as_bytes(gsl::span<const Byte>(asset.inputFiles[0].data)));

	// Write animation
	Animation animation = generateAnimation(baseName, spriteSheetPath.dropFront(1), frames);
	FileSystem::writeFile(dstDir / animationPath, Serializer::toBytes(animation));

	// Split grid
	Vector2i grid(meta.getInt("tileWidth", 0), meta.getInt("tileHeight", 0));
	if (grid.x > 0 && grid.y > 0) {
		frames = splitImagesInGrid(frames, grid);
	}

	// Generate atlas + spritesheet
	SpriteSheet spriteSheet;
	auto atlasImage = generateAtlas(imagePath.dropFront(1), frames, spriteSheet);
	FileSystem::writeFile(dstDir / imagePath, atlasImage->savePNGToBytes());
	FileSystem::writeFile(dstDir / spriteSheetPath, Serializer::toBytes(spriteSheet));

	// Image metafile
	auto size = atlasImage->getSize();
	meta.set("width", size.x);
	meta.set("height", size.y);
	FileSystem::writeFile(dstDir / imageMetaPath, Serializer::toBytes(meta));

	return { spriteSheetPath, animationPath, imagePath, imageMetaPath };
}

std::vector<AsepriteImporter::ImageData> AsepriteImporter::loadImagesFromPath(Path tmp) {
	std::vector<ImageData> frameData;
	for (auto p : FileSystem::enumerateDirectory(tmp)) {
		if (p.getExtension() == ".png") {
			ImageData data;

			auto bytes = FileSystem::readFile(tmp / p);
			data.img = std::make_unique<Image>(p.getFilename().getString(), gsl::as_bytes(gsl::span<Byte>(bytes)), false);
			auto parsedName = p.getStem().getString().split("___");
			if (parsedName.size() != 3 || parsedName[0] != "out") {
				throw Exception("Error parsing filename: " + p.getStem().getString());
			}
			data.sequenceName = parsedName[1];
			data.frameNumber = parsedName[2].toInteger();
			frameData.push_back(std::move(data));
		}
	}
	return frameData;
}

std::map<int, int> AsepriteImporter::getSpriteDurations(Path jsonPath) {
	std::map<int, int> durations;
	SpriteSheet spriteSheet;
	auto jsonData = FileSystem::readFile(jsonPath);
	spriteSheet.loadJson(gsl::as_bytes(gsl::span<Byte>(jsonData)));

	// Load durations
	for (auto& name: spriteSheet.getSpriteNames()) {
		auto& sprite = spriteSheet.getSprite(name);
		auto parsedNames = Path(name).getStem().getString().split("___");
		int f = parsedNames.back().toInteger();
		durations[f] = sprite.duration;
	}

	return durations;
}

void AsepriteImporter::processFrameData(String baseName, std::vector<ImageData>& frameData, std::map<int, int> durations) {
	std::sort(frameData.begin(), frameData.end(), [] (const ImageData& a, const ImageData& b) -> bool {
		return a.frameNumber < b.frameNumber;
	});

	struct TagInfo {
		int num = 0;
		int cur = 0;
	};

	std::map<String, TagInfo> tags;
	for (auto& frame: frameData) {
		tags[frame.sequenceName].num++;
	}

	for (auto& frame: frameData) {
		auto& tag = tags[frame.sequenceName];
		frame.duration = durations[frame.frameNumber];
		frame.frameNumber = tag.cur++;

		bool hasFrameNumber = tag.num > 1;

		std::stringstream ss;
		ss << baseName.cppStr();
		if (frame.sequenceName != "") {
			ss << "_" << frame.sequenceName.cppStr();
		}
		if (hasFrameNumber) {
			ss << "_" << std::setw(3) << std::setfill('0') << frame.frameNumber;
		}
		frame.filename = ss.str();
		frame.img->setName(frame.filename);
	}
}

std::vector<AsepriteImporter::ImageData> AsepriteImporter::importAseprite(String baseName, gsl::span<const gsl::byte> fileData)
{
	// Make temporary folder
	Path tmp = FileSystem::getTemporaryPath();
	FileSystem::createDir(tmp);
	Path tmpFilePath = tmp / "sprite.ase";
	FileSystem::createParentDir(tmpFilePath);
	FileSystem::writeFile(tmpFilePath, fileData);

	// Run aseprite
	Path jsonPath = tmpFilePath.parentPath() / "data.json";
	Path baseOutputPath = tmpFilePath.parentPath() / "out";
	if (FileSystem::runCommand("aseprite -b " + tmpFilePath.getString() + " --list-tags --data " + jsonPath.getString() + " --filename-format {path}/out___{tag}___{frame000}.png --save-as " + baseOutputPath.getString() + ".png") != 0) {
		throw Exception("Unable to execute aseprite.");
	}

	// Load all images
	std::vector<ImageData> frameData = loadImagesFromPath(tmp);
	std::map<int, int> durations = getSpriteDurations(jsonPath);

	// Remove temp
	FileSystem::remove(tmp);

	// Process images
	processFrameData(baseName, frameData, durations);
	return frameData;
}

Animation AsepriteImporter::generateAnimation(String baseName, Path spriteSheetPath, const std::vector<ImageData>& frameData)
{
	Animation animation;

	animation.setName(baseName);
	animation.setMaterialName("sprite");
	animation.setSpriteSheetName(spriteSheetPath.getString());

	std::map<String, AnimationSequence> sequences;

	animation.addDirection(AnimationDirection("right", "forward", false, 0));
	animation.addDirection(AnimationDirection("left", "forward", true, 0));
	
	for (auto& frame: frameData) {
		auto i = sequences.find(frame.sequenceName);
		if (i == sequences.end()) {
			sequences[frame.sequenceName] = AnimationSequence(frame.sequenceName, 1000.0f / float(frame.duration), true, false);
		}
		auto& seq = sequences[frame.sequenceName];

		seq.addFrame(AnimationFrameDefinition(frame.frameNumber, frame.filename));
	}

	for (auto& seq: sequences) {
		animation.addSequence(seq.second);
	}

	return animation;
}

std::unique_ptr<Image> AsepriteImporter::generateAtlas(Path imageName, std::vector<ImageData>& images, SpriteSheet& spriteSheet)
{
	// Generate entries
	std::vector<BinPackEntry> entries;
	for (auto& img: images) {
		entries.push_back(BinPackEntry(img.img->getSize(), &img));
	}

	// Try packing
	int maxSize = 2048;
	int curSize = 32;
	bool wide = false;
	while (curSize < maxSize) {
		Vector2i size(curSize * (wide ? 2 : 1), curSize);
		auto res = BinPack::pack(entries, size);
		if (res.is_initialized()) {
			// Found a pack
			return makeAtlas(imageName, res.get(), size, spriteSheet);
		} else {
			// Try 64x64, then 128x64, 128x128, 256x128, etc
			if (wide) {
				wide = false;
				curSize *= 2;
			} else {
				wide = true;
			}
		}
	}

	throw Exception("Unable to pack sprites in a reasonably sized atlas!");
}

std::unique_ptr<Image> AsepriteImporter::makeAtlas(Path imageName, const std::vector<BinPackResult>& result, Vector2i size, SpriteSheet& spriteSheet)
{
	auto image = std::make_unique<Image>(size.x, size.y);
	image->clear(0);

	spriteSheet.setTextureName(imageName.getString());

	for (auto& packedImg: result) {
		ImageData* img = reinterpret_cast<ImageData*>(packedImg.data);
		image->blitFrom(packedImg.rect.getTopLeft(), *img->img);

		SpriteSheetEntry entry;
		entry.size = Vector2f(img->img->getSize());
		entry.rotated = false;
		entry.pivot = Vector2f();
		entry.coords = Rect4f(packedImg.rect) / Vector2f(size);
		spriteSheet.addSprite(img->filename, entry);
	}

	return image;
}

std::vector<AsepriteImporter::ImageData> AsepriteImporter::splitImagesInGrid(const std::vector<ImageData>& images, Vector2i grid)
{
	std::vector<ImageData> result;

	for (auto& src: images) {
		auto imgSize = src.img->getSize();
		int nX = imgSize.x / grid.x;
		int nY = imgSize.y / grid.y;

		for (int y = 0; y < nY; ++y) {
			for (int x = 0; x < nX; ++x) {
				auto img = std::make_unique<Image>(grid.x, grid.y);
				img->blitFrom(Vector2i(), *src.img, Rect4i(Vector2i(x, y) * grid, grid.x, grid.y));
				Rect4i trimRect = img->getTrimRect();
				if (trimRect.getWidth() > 0 && trimRect.getHeight() > 0) {
					result.push_back(ImageData());
					auto& dst = result.back();

					String suffix = "_" + toString(x) + "_" + toString(y);

					dst.duration = src.duration;
					dst.frameNumber = src.frameNumber;
					dst.filename = src.filename + suffix;
					dst.sequenceName = src.sequenceName + suffix;
					dst.img = std::move(img);
				}
			}
		}
	}

	return result;
}