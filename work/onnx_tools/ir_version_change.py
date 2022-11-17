

import onnx

if __name__=="__main__":
    model = onnx.load_model('/home/ningzhang/workspace_193/customer_proj/tiekeyuan/PaddleOCR/modeldownload/ch_PP-OCRv3_rec_infer/ppocr_rec_new.onnx')

    model.ir_version = 6
    onnx.save(model,"ppocr_rec_new_irchanged.onnx")
