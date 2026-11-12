import Foundation
import Metal

/*
  metal_cosim.swift — Headless Apple Silicon Zero-Copy Metal Compute Co-Verifier
  ==============================================================================
  Pure Swift (`0` `.c/.h/.m` files) driving `Metal.framework` directly on Apple
  Silicon Unified Memory (`MTLResourceOptions.storageModeShared`).
  Compiles `shaders/bloom_core.metal` and executes all 3 `64-byte` compute passes
  over raw `.flow` Manifold Atlas cells for `0-ULP` pixel/mask diffing against
  `Bloom.lean`.
*/

func fail(_ msg: String) -> Never {
    FileHandle.standardError.write(Data(("METAL_COSIM_ERR: " + msg + "\n").utf8))
    exit(1)
}

func readU32LE(_ data: Data, _ offset: Int) -> UInt32 {
    let b0 = UInt32(data[offset])
    let b1 = UInt32(data[offset + 1]) << 8
    let b2 = UInt32(data[offset + 2]) << 16
    let b3 = UInt32(data[offset + 3]) << 24
    return b0 | b1 | b2 | b3
}

let args = CommandLine.arguments
guard args.count >= 3 else {
    fail("usage: metal_cosim <--compile-only|--run> <bloom_core.metal> [work_dir]")
}

let mode = args[1]
let shaderPath = args[2]

guard let device = MTLCreateSystemDefaultDevice() else {
    if ProcessInfo.processInfo.environment["BLOOM_CI_ALLOW_NO_GPU"] == "1" {
        print("METAL_NO_GPU_SKIP")
        exit(0)
    }
    fail("MTLCreateSystemDefaultDevice() returned nil")
}

let shaderSource: String
do {
    shaderSource = try String(contentsOfFile: shaderPath, encoding: .utf8)
} catch {
    fail("cannot read shader at \(shaderPath): \(error)")
}

let compileOpts = MTLCompileOptions()
if #available(macOS 15.0, *) {
    compileOpts.mathMode = .safe
}

let library: MTLLibrary
do {
    library = try device.makeLibrary(source: shaderSource, options: compileOpts)
} catch {
    fail("Metal shader compilation failed: \(error)")
}

guard let fnCull  = library.makeFunction(name: "bloom_wave64_tile_cull"),
      let fnMarch = library.makeFunction(name: "bloom_sdf_matter_march"),
      let fnComp  = library.makeFunction(name: "bloom_bento_glass_composite") else {
    fail("missing required kernel entrypoints in \(shaderPath)")
}

let psoCull: MTLComputePipelineState
let psoMarch: MTLComputePipelineState
let psoComp: MTLComputePipelineState
do {
    psoCull  = try device.makeComputePipelineState(function: fnCull)
    psoMarch = try device.makeComputePipelineState(function: fnMarch)
    psoComp  = try device.makeComputePipelineState(function: fnComp)
} catch {
    fail("failed to create Metal compute pipeline states: \(error)")
}

if mode == "--compile-only" {
    print("METAL_COMPILE_OK device=\"\(device.name)\" unified=\(device.hasUnifiedMemory)")
    exit(0)
}

guard mode == "--run", args.count >= 4 else {
    fail("expected: metal_cosim --run <bloom_core.metal> <work_dir>")
}

let workDir = URL(fileURLWithPath: args[3], isDirectory: true)
let hdrData: Data
let primData: Data
let widData: Data
let flowData: Data
do {
    hdrData  = try Data(contentsOf: workDir.appendingPathComponent("header.bin"))
    primData = try Data(contentsOf: workDir.appendingPathComponent("prims.bin"))
    widData  = try Data(contentsOf: workDir.appendingPathComponent("widgets.bin"))
    flowData = try Data(contentsOf: workDir.appendingPathComponent("flow_cells.bin"))
} catch {
    fail("failed reading input buffers in \(workDir.path): \(error)")
}

guard hdrData.count == 64 else {
    fail("header.bin must be strictly 64 bytes, got \(hdrData.count)")
}
guard primData.count % 64 == 0 && widData.count % 64 == 0 && flowData.count % 64 == 0 else {
    fail("all input descriptor buffers must be 64-byte aligned")
}

let width    = Int(readU32LE(hdrData, 0))
let height   = Int(readU32LE(hdrData, 4))
let tileCols = Int(readU32LE(hdrData, 8))
let tileRows = Int(readU32LE(hdrData, 12))

func makeSharedInputBuffer(_ data: Data) -> MTLBuffer {
    let safeLen = max(data.count, 64)
    guard let buf = device.makeBuffer(length: safeLen, options: .storageModeShared) else {
        fail("failed allocating shared MTLBuffer (\(safeLen) bytes)")
    }
    if !data.isEmpty {
        data.copyBytes(to: buf.contents().assumingMemoryBound(to: UInt8.self), count: data.count)
    }
    return buf
}

let hdrBuf  = makeSharedInputBuffer(hdrData)
let primBuf = makeSharedInputBuffer(primData)
let widBuf  = makeSharedInputBuffer(widData)
let flowBuf = makeSharedInputBuffer(flowData)

let maskBytes  = max(tileCols * tileRows * 2 * 4, 8)
let pixelBytes = max(width * height * 4, 4)

guard let maskBuf  = device.makeBuffer(length: maskBytes,  options: .storageModeShared),
      let sceneBuf = device.makeBuffer(length: pixelBytes, options: .storageModeShared),
      let finalBuf = device.makeBuffer(length: pixelBytes, options: .storageModeShared) else {
    fail("failed allocating output MTLBuffers")
}

guard let queue = device.makeCommandQueue(),
      let cmdBuf = queue.makeCommandBuffer() else {
    fail("failed creating Metal command queue / buffer")
}

let tg8x8 = MTLSize(width: 8, height: 8, depth: 1)

if let enc1 = cmdBuf.makeComputeCommandEncoder() {
    enc1.setComputePipelineState(psoCull)
    enc1.setBuffer(hdrBuf,  offset: 0, index: 0)
    enc1.setBuffer(widBuf,  offset: 0, index: 1)
    enc1.setBuffer(maskBuf, offset: 0, index: 2)
    enc1.dispatchThreads(MTLSize(width: tileCols, height: tileRows, depth: 1),
                         threadsPerThreadgroup: tg8x8)
    enc1.endEncoding()
}

if let enc2 = cmdBuf.makeComputeCommandEncoder() {
    enc2.setComputePipelineState(psoMarch)
    enc2.setBuffer(hdrBuf,   offset: 0, index: 0)
    enc2.setBuffer(primBuf,  offset: 0, index: 1)
    enc2.setBuffer(sceneBuf, offset: 0, index: 2)
    enc2.dispatchThreads(MTLSize(width: width, height: height, depth: 1),
                         threadsPerThreadgroup: tg8x8)
    enc2.endEncoding()
}

if let enc3 = cmdBuf.makeComputeCommandEncoder() {
    enc3.setComputePipelineState(psoComp)
    enc3.setBuffer(hdrBuf,   offset: 0, index: 0)
    enc3.setBuffer(widBuf,   offset: 0, index: 1)
    enc3.setBuffer(flowBuf,  offset: 0, index: 2)
    enc3.setBuffer(maskBuf,  offset: 0, index: 3)
    enc3.setBuffer(sceneBuf, offset: 0, index: 4)
    enc3.setBuffer(finalBuf, offset: 0, index: 5)
    enc3.dispatchThreads(MTLSize(width: width, height: height, depth: 1),
                         threadsPerThreadgroup: tg8x8)
    enc3.endEncoding()
}

cmdBuf.commit()
cmdBuf.waitUntilCompleted()

if cmdBuf.status != .completed {
    fail("GPU command buffer failed with status \(cmdBuf.status.rawValue): \(String(describing: cmdBuf.error))")
}

let outMasks = Data(bytes: maskBuf.contents(),  count: tileCols * tileRows * 2 * 4)
let outScene = Data(bytes: sceneBuf.contents(), count: width * height * 4)
let outFinal = Data(bytes: finalBuf.contents(), count: width * height * 4)

do {
    try outMasks.write(to: workDir.appendingPathComponent("gpu_masks.bin"))
    try outScene.write(to: workDir.appendingPathComponent("gpu_scene.bin"))
    try outFinal.write(to: workDir.appendingPathComponent("gpu_final.bin"))
} catch {
    fail("failed writing GPU output buffers: \(error)")
}

print("METAL_RUN_OK device=\"\(device.name)\" unified=\(device.hasUnifiedMemory) pixels=\(width * height) tiles=\(tileCols * tileRows)")
