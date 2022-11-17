import onnx
import onnxruntime
import numpy as np

if __name__ == '__main__':
    # model_path = "ppyoloe_plus_crn_s_80e_coco_inferedshape.onnx"
    # model_path = 'test1_onnxV7_ppyoloe.onnx'
    model_path = './ch_ppocr_rec.onnx'

    # # onnx.checker.check_model(model_path)
    # input_data = {
    #     'scale_factor': np.zeros((1,2)).astype(np.float32),
    #     'image': np.zeros((1,3,640,640)).astype(np.float32),
    # }
    input_data = {
        # 'scale_factor': np.zeros((1,2)).astype(np.float32),
        'x': np.zeros((1,3,100,200)).astype(np.float32),
    }

    model = onnx.load(model_path)
    onnx_session = onnxruntime.InferenceSession(model.SerializeToString())

    outputs_data = onnx_session.run([node.name for node in onnx_session.get_outputs()], input_data)

    print(outputs_data)
    print('end')
    # cpu_output = cpu_infer(model_path, input_datas)