/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// Translated from FreeSims sims.files/formats/iff/chunks/SPR.cs
#include "files/formats/iff/chunks/SPR.h"
#include "files/formats/iff/IffFile.h"
#include "files/FilePool.h"
#include <limits>
#include <stdexcept>

namespace FSO { namespace Files { namespace Formats { namespace IFF { namespace Chunks {

PALT SPRFrame::DEFAULT_PALT(Color::Black());

void SPR::Read(IffFile* iff, st::Reader& stream)
{
    (void)iff;
    st::Reader& io = stream;
    io.ByteOrder(st::Endian::Little);
    {
        auto version1 = io.ReadUInt16();
        auto version2 = io.ReadUInt16();
        uint32_t version = 0;

        if (version1 == 0)
        {
            io.ByteOrder(st::Endian::Big);
            version = (uint32_t)(((version2|0xFF00)>>8) | ((version2&0xFF)<<8));
        }
        else
        {
            version = version1;
        }
        ByteOrd = io.ByteOrder();

        auto spriteCount = io.ReadUInt32();
        PaletteID = (uint16_t)io.ReadUInt32();

        Frames.clear();
        if (version != 1001)
        {
            if (spriteCount > (io.size() - io.pos()) / sizeof(uint32_t))
                throw std::runtime_error("SPR offset table exceeds chunk");
            std::vector<uint32_t> offsetTable;
            offsetTable.reserve(spriteCount);
            for (uint32_t i = 0; i < spriteCount; i++)
            {
                offsetTable.push_back(io.ReadUInt32());
            }
            const size_t frameDataStart = io.pos();
            for (size_t i = 0; i < offsetTable.size(); i++)
            {
                if (offsetTable[i] < frameDataStart || offsetTable[i] >= io.size() ||
                    (i + 1 < offsetTable.size() && offsetTable[i + 1] <= offsetTable[i]))
                    throw std::runtime_error("invalid SPR frame offset");
            }
            Offsets = offsetTable;
            for (uint32_t i = 0; i < spriteCount; i++)
            {
                auto frame = std::make_unique<SPRFrame>(this);
                io.Seek(st::SeekOrigin::Begin, offsetTable[i]);
                size_t frameEnd = i + 1 < offsetTable.size() ? offsetTable[i + 1] : io.size();
                if (frameEnd - offsetTable[i] > std::numeric_limits<uint32_t>::max())
                    throw std::runtime_error("SPR frame is too large");
                auto guessedSize = (uint32_t)(frameEnd - offsetTable[i]);
                frame->Read(version, io, guessedSize);
                Frames.emplace_back(std::move(frame));
            }
        }
        else
        {
            while (io.HasMore())
            {
                auto frame = std::make_unique<SPRFrame>(this);
                frame->Read(version, io, 0);
                Frames.emplace_back(std::move(frame));
            }
        }
    }
}

void SPRFrame::Read(uint32_t version, st::Reader& io, uint32_t guessedSize)
{
    // FIX(sims) memory: see SPR.h (frames of read-only files are read again from the file when decoded)
    bool onDisk = Parent != nullptr && Parent->SourceOffset >= 0 && Parent->ChunkParent != nullptr
                  && !Parent->ChunkParent->SourcePath.empty();
    auto ensureAvailable = [&](uint32_t size) {
        if (io.pos() > io.size() || size > io.size() - io.pos())
            throw std::runtime_error("truncated SPR frame");
    };
    auto lazy = [&](uint32_t size) {
        ensureAvailable(size);
        SrcOffset = Parent->SourceOffset + 76 + (int64_t)io.pos();
        SrcSize = size;
        io.Skip(size);
        HasToDecode = true;
    };
    if (version == 1001)
    {
        auto spriteFersion = io.ReadUInt32();

        auto size = io.ReadUInt32();
        this->Version = spriteFersion;

        ensureAvailable(size);
        if (IffFile::RETAIN_CHUNK_DATA) {
            auto data = io.ReadBytes(size);
            st::Reader frameData(data, io.ByteOrder());
            ReadDeferred(1001, frameData);
        }
        else if (onDisk) lazy(size);
        else { ToDecode = io.ReadBytes(size); HasToDecode = true; }
    }
    else
    {
        this->Version = version;
        ensureAvailable(guessedSize);
        if (IffFile::RETAIN_CHUNK_DATA) {
            auto data = io.ReadBytes(guessedSize);
            st::Reader frameData(data, io.ByteOrder());
            ReadDeferred(1000, frameData);
        }
        else if (onDisk) lazy(guessedSize);
        else { ToDecode = io.ReadBytes(guessedSize); HasToDecode = true; }
    }
}

void SPRFrame::ReadDeferred(uint32_t version, st::Reader& io)
{
    (void)version;
    auto reserved = io.ReadUInt32();
    (void)reserved;
    auto height = io.ReadUInt16();
    auto width = io.ReadUInt16();
    this->Init(width, height);
    this->Decode(io);
}

void SPRFrame::DecodeIfRequired()
{
    if (HasToDecode)
    {
        if (ToDecode.empty() && SrcOffset >= 0) // FIX(sims): compressed bytes are read back from the file
        {
            std::string err;
            if (!FSO::Files::FilePool::ReadAt(Parent->ChunkParent->SourcePath, SrcOffset, SrcSize, ToDecode, &err))
            {
                ToDecode.clear();
                HasToDecode = false; // an unreadable frame stays empty
                return;
            }
        }
        {
            st::Reader buf(ToDecode, Parent->ByteOrd);
            ReadDeferred(Version, buf);
        }

        ToDecode.clear();
        ToDecode.shrink_to_fit();
        HasToDecode = false;
    }
}

std::vector<Color> SPRFrame::ExpandColors() // FIX(sims): see SPR.h
{
    DecodeIfRequired();
    std::vector<Color> out;
    if (!HasColor()) return out;
    out.resize(Index.size());
    for (size_t i = 0; i < out.size(); i++) out[i] = ColorAt(i);
    return out;
}

size_t SPRFrame::DropDecoded() // FIX(sims): see SPR.h
{
    if (HasToDecode || SrcOffset < 0) return 0;
    size_t bytes = Index.capacity() + Alpha.capacity();
    std::vector<uint8_t>().swap(Index);
    std::vector<uint8_t>().swap(Alpha);
    HasToDecode = true;
    return bytes;
}

void SPRFrame::DetachFromSource() // FIX(sims): IffFile::DetachSource
{
    if (SrcOffset < 0) return;
    if (HasToDecode && ToDecode.empty()) {
        std::string err;
        if (!FSO::Files::FilePool::ReadAt(Parent->ChunkParent->SourcePath, SrcOffset, SrcSize, ToDecode, &err)) { ToDecode.clear(); HasToDecode = false; }
    }
    SrcOffset = -1;
}

void SPRFrame::Decode(st::Reader& io)
{
    auto palette = Parent->ChunkParent->Get<PALT>(Parent->PaletteID);
    if (palette == nullptr)
    {
        palette = &DEFAULT_PALT;
    }
    // FIX(sims): frames index the colours directly (see SPR.h); short palettes are padded once, before any frame
    // keeps a pointer to them
    if (palette->Colors.size() < 256) palette->Colors.resize(256);
    PalColors = palette->Colors.data();

    int32_t y = 0;
    bool endmarker = false;

    while (!endmarker){
        auto command = io.ReadByte();
        auto count = io.ReadByte();

        switch (command){
            /** Start marker **/
            case 0x00:
            case 0x10:
                break;
            /** Fill row with pixel data **/
            case 0x04:
            {
                int32_t bytes = count - 2;
                int32_t x = 0;

                while (bytes > 0){
                    auto pxCommand = io.ReadByte();
                    auto pxCount = io.ReadByte();
                    bytes -= 2;

                    switch (pxCommand){
                        /** Next {n} pixels are transparent **/
                        case 0x01:
                            x += pxCount;
                            break;
                        /** Next {n} pixels are the same palette color **/
                        case 0x02:
                        {
                            auto index = io.ReadByte();
                            auto padding = io.ReadByte();
                            (void)padding;
                            bytes -= 2;

                            uint8_t a = palette->Colors[index].A;
                            for (int j=0; j < pxCount; j++){
                                if (x >= 0 && x < Width && y >= 0 && y < Height) { // FIX(sims): palettised (see SPR.h)
                                    size_t o = (size_t)y * (size_t)Width + (size_t)x;
                                    Index[o] = index; Alpha[o] = a;
                                }
                                x++;
                            }
                            break;
                        }
                        /** Next {n} pixels are specific palette colours **/
                        case 0x03:
                            for (int j=0; j < pxCount; j++){
                                auto index2 = io.ReadByte();
                                if (x >= 0 && x < Width && y >= 0 && y < Height) { // FIX(sims): palettised (see SPR.h)
                                    size_t o = (size_t)y * (size_t)Width + (size_t)x;
                                    Index[o] = index2; Alpha[o] = palette->Colors[index2].A;
                                }
                                x++;
                            }
                            bytes -= pxCount;
                            if (pxCount % 2 != 0){
                                //Padding
                                io.ReadByte();
                                bytes--;
                            }
                            break;
                    }
                }

                y++;
                break;
            }
            /** End marker **/
            case 0x05:
                endmarker = true;
                break;
            /** Leave next rows transparent **/
            case 0x09:
                y += count;
                continue;
        }

    }
}

Utils::Texture2D* SPRFrame::GetTexture(Utils::GraphicsDevice* device)
{
    (void)device;
    DecodeIfRequired();
    // STUB(3ds): C# creates a Texture2D(max(1,Width), max(1,Height)) from Data (or a single
    // transparent pixel when empty) and drops Data unless RETAIN_CHUNK_DATA. Here Data is kept
    // and no texture is created.
    return PixelCache;
}

}}}}} // namespace FSO::Files::Formats::IFF::Chunks
