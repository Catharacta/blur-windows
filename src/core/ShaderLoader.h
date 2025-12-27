#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <fstream>
#include "Logger.h"

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace blurwindow {

/// Shader loading and compilation utilities
class ShaderLoader {
public:
    /// Load a pre-compiled vertex shader from file (.cso)
    static bool LoadVertexShader(
        ID3D11Device* device,
        const wchar_t* path,
        ID3D11VertexShader** outShader,
        std::vector<uint8_t>* outBytecode = nullptr
    ) {
        std::vector<uint8_t> bytecode;
        if (!LoadBytecodeFromFile(path, bytecode)) {
            return false;
        }

        HRESULT hr = device->CreateVertexShader(
            bytecode.data(), bytecode.size(),
            nullptr, outShader
        );

        if (SUCCEEDED(hr) && outBytecode) {
            *outBytecode = std::move(bytecode);
        }

        return SUCCEEDED(hr);
    }

    /// Load a pre-compiled pixel shader from file (.cso)
    static bool LoadPixelShader(
        ID3D11Device* device,
        const wchar_t* path,
        ID3D11PixelShader** outShader
    ) {
        std::vector<uint8_t> bytecode;
        if (!LoadBytecodeFromFile(path, bytecode)) {
            return false;
        }

        HRESULT hr = device->CreatePixelShader(
            bytecode.data(), bytecode.size(),
            nullptr, outShader
        );

        return SUCCEEDED(hr);
    }

    /// Compile vertex shader from source at runtime
    static bool CompileVertexShader(
        ID3D11Device* device,
        const char* source,
        size_t sourceSize,
        const char* entryPoint,
        ID3D11VertexShader** outShader,
        std::vector<uint8_t>* outBytecode = nullptr
    ) {
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;

        UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

        HRESULT hr = D3DCompile(
            source, sourceSize,
            nullptr, nullptr, nullptr,
            entryPoint, "vs_5_0",
            compileFlags, 0,
            shaderBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );

        if (FAILED(hr)) {
            if (errorBlob) {
                LOG_ERROR("VS Compilation Error: %s", (const char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        hr = device->CreateVertexShader(
            shaderBlob->GetBufferPointer(),
            shaderBlob->GetBufferSize(),
            nullptr, outShader
        );

        if (SUCCEEDED(hr) && outBytecode) {
            outBytecode->resize(shaderBlob->GetBufferSize());
            memcpy(outBytecode->data(), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
        }

        return SUCCEEDED(hr);
    }

    /// Compile pixel shader from source at runtime
    static bool CompilePixelShader(
        ID3D11Device* device,
        const char* source,
        size_t sourceSize,
        const char* entryPoint,
        ID3D11PixelShader** outShader
    ) {
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;

        UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

        HRESULT hr = D3DCompile(
            source, sourceSize,
            nullptr, nullptr, nullptr,
            entryPoint, "ps_5_0",
            compileFlags, 0,
            shaderBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );

        if (FAILED(hr)) {
            if (errorBlob) {
                LOG_ERROR("PS Compilation Error: %s", (const char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        hr = device->CreatePixelShader(
            shaderBlob->GetBufferPointer(),
            shaderBlob->GetBufferSize(),
            nullptr, outShader
        );

        return SUCCEEDED(hr);
    }

    /// Create shader from pre-compiled bytecode
    static bool CreateVertexShaderFromBytecode(
        ID3D11Device* device,
        const void* bytecode,
        size_t bytecodeSize,
        ID3D11VertexShader** outShader
    ) {
        return SUCCEEDED(device->CreateVertexShader(
            bytecode, bytecodeSize, nullptr, outShader
        ));
    }

    static bool CreatePixelShaderFromBytecode(
        ID3D11Device* device,
        const void* bytecode,
        size_t bytecodeSize,
        ID3D11PixelShader** outShader
    ) {
        return SUCCEEDED(device->CreatePixelShader(
            bytecode, bytecodeSize, nullptr, outShader
        ));
    }

private:
    static bool LoadBytecodeFromFile(const wchar_t* path, std::vector<uint8_t>& outBytecode) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }

        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        outBytecode.resize(size);
        file.read(reinterpret_cast<char*>(outBytecode.data()), size);

        return file.good();
    }
};

} // namespace blurwindow
