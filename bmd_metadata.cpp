/* -LICENSE-START-
 ** Copyright (c) 2018 Blackmagic Design
 **
 ** Permission is hereby granted, free of charge, to any person or organization
 ** obtaining a copy of the software and accompanying documentation covered by
 ** this license (the "Software") to use, reproduce, display, distribute,
 ** execute, and transmit the Software, and to prepare derivative works of the
 ** Software, and to permit third-parties to whom the Software is furnished to
 ** do so, all subject to the following:
 **
 ** The copyright notices in the Software and this entire statement, including
 ** the above license grant, this restriction and the following disclaimer,
 ** must be included in all copies of the Software, in whole or in part, and
 ** all derivative works of the Software, unless such copies or derivative
 ** works are solely in the form of machine-executable object code generated by
 ** a source language processor.
 **
 ** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 ** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 ** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 ** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 ** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 ** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 ** DEALINGS IN THE SOFTWARE.
 ** -LICENSE-END-
 */

#include "BlackmagicRawAPI.h"

#include <cassert>
#include <atomic>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <boost/dll/runtime_symbol_info.hpp>

#include "bmd_metadata.h"

//using namespace std::__fs;
//using namespace std::filesystem;

const std::string versionString = "0.1.0dev-20231214";

static const int BUFSIZE = 1024;
class CameraCodecCallback : public IBlackmagicRawCallback
{
public:
	CameraCodecCallback() : m_refCount(0) {}

	CameraCodecCallback(const CameraCodecCallback&) = delete;
	CameraCodecCallback& operator=(const CameraCodecCallback&) = delete;
	//explicit CameraCodecCallback() = default;
	virtual ~CameraCodecCallback()
	{
		assert(m_refCount == 0);
		SetFrame(nullptr);
	}

	IBlackmagicRawFrame*	GetFrame() { return m_frame; }

	virtual void ReadComplete(IBlackmagicRawJob* readJob, HRESULT result, IBlackmagicRawFrame* frame)
	{
		if (result == S_OK)
		{
			SetFrame(frame);
		}
	}

	virtual void ProcessComplete(IBlackmagicRawJob* job, HRESULT result, IBlackmagicRawProcessedImage* processedImage) {}
	virtual void DecodeComplete(IBlackmagicRawJob*, HRESULT) {}
	virtual void TrimProgress(IBlackmagicRawJob*, float) {}
	virtual void TrimComplete(IBlackmagicRawJob*, HRESULT) {}
	virtual void SidecarMetadataParseWarning(IBlackmagicRawClip*, CFStringRef, uint32_t, CFStringRef) {}
	virtual void SidecarMetadataParseError(IBlackmagicRawClip*, CFStringRef, uint32_t, CFStringRef) {}
	virtual void PreparePipelineComplete(void*, HRESULT) {}

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*)
	{
		return E_NOTIMPL;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef(void)
	{
		return ++m_refCount;
	}

	virtual ULONG STDMETHODCALLTYPE Release(void)
	{
		const int32_t newRefValue = --m_refCount;

		if (newRefValue == 0)
		{
			delete this;
		}

		assert(newRefValue >= 0);
		return newRefValue;
	}

private:

	void SetFrame(IBlackmagicRawFrame* frame)
	{
		if (m_frame != nullptr)
			m_frame->Release();
		m_frame = frame;
		if (m_frame != nullptr)
			m_frame->AddRef();
	}

	IBlackmagicRawFrame* m_frame = nullptr;
	//std::atomic<int32_t> m_refCount = {0};
	std::atomic<int32_t> m_refCount;
};

std::map<std::string, Variant> GetMetadataMap(IBlackmagicRawMetadataIterator* metadataIterator)
{
    std::map<std::string, Variant> metadataMap;
    CFStringRef key = nullptr;
    Variant value;

    while (SUCCEEDED(metadataIterator->GetKey(&key)))
    {
        char buffer[BUFSIZE];
        if (CFStringGetCString(key, buffer, BUFSIZE, kCFStringEncodingMacRoman))
        {
            std::string str = buffer;
            metadataIterator->GetData(&value);
            metadataMap[str] = value;
        }

        metadataIterator->Next();
    }

    return metadataMap;
}

void PrintVariant(const Variant& value, std::ostringstream& oss)
{
    char buffer[BUFSIZE];
    const char *str = nullptr;
    // Check the type of the variant and print accordingly
    switch (value.vt)
    {
        case blackmagicRawVariantTypeS16:
        {
            short s16 = value.iVal;
            oss << s16;
        }
        break;
        case blackmagicRawVariantTypeU16:
        {
            unsigned short u16 = value.uiVal;
            oss << u16;
        }
        break;
        case blackmagicRawVariantTypeS32:
        {
            int i32 = value.intVal;
            oss << i32;
        }
        break;
        case blackmagicRawVariantTypeU32:
        {
            unsigned int u32 = value.uintVal;
            oss << u32;
        }
        break;
        case blackmagicRawVariantTypeFloat32:
        {
            float f32 = value.fltVal;
            oss << f32;
        }
        break;
        case blackmagicRawVariantTypeString:
        {
            if (CFStringGetCString(value.bstrVal, buffer, BUFSIZE, kCFStringEncodingMacRoman))
            {
                str = buffer;
                oss << str;
            }
        }
        break;
        case blackmagicRawVariantTypeSafeArray:
        {
        SafeArray* safeArray = value.parray;

        void* safeArrayData = nullptr;
        HRESULT result = SafeArrayAccessData(safeArray, &safeArrayData);
        if (result != S_OK)
        {
            std::cerr << "Failed to access safeArray data!" << std::endl;
            break;
        }

        BlackmagicRawVariantType arrayVarType;
        result = SafeArrayGetVartype(safeArray, &arrayVarType);
        if (result != S_OK)
        {
            std::cerr << "Failed to get BlackmagicRawVariantType from safeArray!" << std::endl;
            SafeArrayUnaccessData(safeArray);
            break;
        }

        long lBound;
        result = SafeArrayGetLBound(safeArray, 1, &lBound);
        if (result != S_OK)
        {
            std::cerr << "Failed to get LBound from safeArray!" << std::endl;
            SafeArrayUnaccessData(safeArray);
            break;
        }
        	if (result != S_OK)
        	{
        		std::cerr << "Failed to get LBound from safeArray!" << std::endl;
        		break;
        	}

        	long uBound;
        	result = SafeArrayGetUBound(safeArray, 1, &uBound);
        	if (result != S_OK)
        	{
        		std::cerr << "Failed to get UBound from safeArray!" << std::endl;
        		break;
        	}

        	long safeArrayLength = (uBound - lBound) + 1;
        	long arrayLength = safeArrayLength > 32 ? 32 : safeArrayLength;

        	for (int i = 0; i < arrayLength; ++i)
        	{
        		switch (arrayVarType)
        		{
        			case blackmagicRawVariantTypeU8:
        			{
        				int u8 = static_cast<int>(static_cast<unsigned char*>(safeArrayData)[i]);
        				if (i > 0)
        					oss << ",";
        				oss << u8;
        			}
        			break;
        			case blackmagicRawVariantTypeS16:
        			{
        				short s16 = static_cast<short*>(safeArrayData)[i];
        				oss << s16 << " ";
        			}
        			break;
        			case blackmagicRawVariantTypeU16:
        			{
        				unsigned short u16 = static_cast<unsigned short*>(safeArrayData)[i];
        				oss << u16 << " ";
        			}
        			break;
        			case blackmagicRawVariantTypeS32:
        			{
        				int i32 = static_cast<int*>(safeArrayData)[i];
        				oss << i32 << " ";
        			}
        			break;
        			case blackmagicRawVariantTypeU32:
        			{
        				unsigned int u32 = static_cast<unsigned int*>(safeArrayData)[i];
        				oss << u32 << " ";
        			}
        			break;
        			case blackmagicRawVariantTypeFloat32:
        			{
        				float f32 = static_cast<float*>(safeArrayData)[i];
        				oss << f32 << " ";
        			}
        			break;
        			default:
        				break;
        		}
        	}
        }
        default:
            break;
    }

}

std::map<std::string, Variant> ProcessClip(CFStringRef clipName, CFStringRef binaryDirectoryCFStr)
{
    HRESULT result = S_OK;

	IBlackmagicRawFactory* factory = nullptr;
	IBlackmagicRaw* codec = nullptr;
	IBlackmagicRawClip* clip = nullptr;
	IBlackmagicRawMetadataIterator* clipMetadataIterator = nullptr;
	IBlackmagicRawMetadataIterator* frameMetadataIterator = nullptr;
	CameraCodecCallback* callback = nullptr;

    std::map<std::string, Variant> metadataMap;

	do
	{
		factory = CreateBlackmagicRawFactoryInstanceFromPath(binaryDirectoryCFStr);
		if (factory == nullptr)
		{
			result = E_FAIL;
			std::cerr << "Failed to create IBlackmagicRawFactory!" << std::endl;
			break;
		}

		result = factory->CreateCodec(&codec);
		if (result != S_OK)
		{
			std::cerr << "Failed to create IBlackmagicRaw!" << std::endl;
			break;
		}

		result = codec->OpenClip(clipName, &clip);
		if (result != S_OK)
		{
			std::cerr << "Failed to open IBlackmagicRawClip!" << std::endl;
			break;
		}

		callback = new CameraCodecCallback();
		callback->AddRef();

		result = codec->SetCallback(callback);
		if (result != S_OK)
		{
			std::cerr << "Failed to set IBlackmagicRawCallback!" << std::endl;
			break;
		}

		result = clip->GetMetadataIterator(&clipMetadataIterator);
		if (result != S_OK)
		{
			std::cerr << "Failed to get clip IBlackmagicRawMetadataIterator!" << std::endl;
			break;
		}

		IBlackmagicRawJob* readJob = nullptr;

		result = clip->CreateJobReadFrame(0, &readJob);
		if (result != S_OK)
		{
			std::cerr << "Failed to get IBlackmagicRawJob!" << std::endl;
			break;
		}

		result = readJob->Submit();
		readJob->Release();
		if (result != S_OK)
		{
			std::cerr << "Failed to submit IBlackmagicRawJob!" << std::endl;
			break;
		}

		codec->FlushJobs();

		IBlackmagicRawFrame* frame = callback->GetFrame();

		if (frame == nullptr)
		{
			std::cerr << "Failed to get IBlackmagicRawFrame!" << std::endl;
			break;
		}

		result = frame->GetMetadataIterator(&frameMetadataIterator);
		if (result != S_OK)
		{
			std::cerr << "Failed to get frame IBlackmagicRawMetadataIterator!" << std::endl;
			break;
		}


        //const char* outputPathCString = CFStringGetCStringPtr(outputPath, kCFStringEncodingUTF8);
        const char* clipNameCString = CFStringGetCStringPtr(clipName, kCFStringEncodingUTF8);

        std::map<std::string, Variant> clipMetadataMap = GetMetadataMap(clipMetadataIterator);
        std::map<std::string, Variant> frameMetadataMap = GetMetadataMap(frameMetadataIterator);

        metadataMap.insert(clipMetadataMap.begin(), clipMetadataMap.end());
        metadataMap.insert(frameMetadataMap.begin(), frameMetadataMap.end());

	} while(0);

	if (callback != nullptr)
		callback->Release();

	if (clipMetadataIterator != nullptr)
		clipMetadataIterator->Release();

	if (frameMetadataIterator != nullptr)
		frameMetadataIterator->Release();

	if (clip != nullptr)
		clip->Release();

	if (codec != nullptr)
		codec->Release();

	if (factory != nullptr)
		factory->Release();

	CFRelease(clipName);

    // Split the field "crop_size" into "image_width" and "image_height"

    std::ostringstream oss;
    PrintVariant(metadataMap["crop_size"], oss); 
    std::string crop_size = oss.str();
    std::string image_width, image_height;

    //std::cout << "crop_size: " << crop_size << "\n";

    std::istringstream iss(crop_size);
    std::string width, height;
    std::getline(iss, width, ' ');
    std::getline(iss, height, ' ');
    // std::cout << "part1: " << part1 << "\n";
    // std::cout << "part2: " << part2 << "\n";

    CFStringRef widthRef = CFStringCreateWithCString(NULL, width.c_str(), kCFStringEncodingUTF8);

    Variant image_width_variant;
    image_width_variant.vt = blackmagicRawVariantTypeString;
    image_width_variant.bstrVal = widthRef;

    metadataMap["image_width"] = image_width_variant;

    CFStringRef heightRef = CFStringCreateWithCString(NULL, height.c_str(), kCFStringEncodingUTF8);

    Variant image_height_variant;
    image_height_variant.vt = blackmagicRawVariantTypeString;
    image_height_variant.bstrVal = heightRef;

    metadataMap["image_height"] = image_height_variant;

    return metadataMap;

}

std::map<std::string, std::string> read_metadata(const std::string filename)
{
    std::string binaryDirectory = (boost::dll::this_line_location().parent_path() / "Libraries").string();

    CFStringRef binaryDirectoryCFStr = CFStringCreateWithCString(NULL, binaryDirectory.c_str(), kCFStringEncodingUTF8);

    std::map<std::string, Variant> metadataMap;
    CFStringRef clipName = CFStringCreateWithCString(NULL, filename.c_str(), kCFStringEncodingUTF8);
    metadataMap = ProcessClip(clipName, binaryDirectoryCFStr);

	std::map<std::string, std::string> stringMap;

	for (auto& kv : metadataMap) {
		std::ostringstream oss;
		PrintVariant(kv.second, oss);
		std::string strValue = oss.str();
		stringMap[kv.first] = strValue;
	}

	stringMap["version"] = versionString;

    return stringMap;
}

