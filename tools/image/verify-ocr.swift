import Foundation
import Vision
import AppKit
let root = URL(fileURLWithPath: CommandLine.arguments[1])
let files = try FileManager.default.contentsOfDirectory(at: root, includingPropertiesForKeys:nil).filter{$0.pathExtension.lowercased()=="png"}.sorted{$0.lastPathComponent < $1.lastPathComponent}
var rows: [[String:Any]] = []
for file in files {
 let req=VNRecognizeTextRequest()
 req.recognitionLevel = .accurate
 req.recognitionLanguages = ["zh-Hans","ja-JP","en-US"]
 req.usesLanguageCorrection=false
 do {
  try VNImageRequestHandler(url:file).perform([req])
  let text=(req.results ?? []).compactMap{$0.topCandidates(1).first?.string}
  let boxes=(req.results ?? []).map{obs -> [String:Any] in
    let b=obs.boundingBox
    return ["text":obs.topCandidates(1).first?.string ?? "", "box":[b.minX,b.minY,b.width,b.height], "confidence":obs.topCandidates(1).first?.confidence ?? 0]
  }
  rows.append(["file":file.lastPathComponent,"text":text,"boxes":boxes])
 } catch { rows.append(["file":file.lastPathComponent,"error":String(describing:error)]) }
}
let data=try JSONSerialization.data(withJSONObject:rows,options:[.prettyPrinted,.sortedKeys,.withoutEscapingSlashes])
try data.write(to:URL(fileURLWithPath:CommandLine.arguments[2]))
