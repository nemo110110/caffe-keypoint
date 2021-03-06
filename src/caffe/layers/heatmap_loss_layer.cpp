#include <vector>

#include "caffe/layer.hpp"
#include "caffe/util/io.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/layers/loss_layer.hpp"
#include "caffe/layers/heatmap_loss_layer.hpp"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc/imgproc.hpp>


// Euclidean loss layer that computes loss on an [x] x [y] x [ch] set of heatmaps,
// and enables visualisation of inputs, GT, prediction and loss.


namespace caffe {

template <typename Dtype>
void HeatmapLossLayer<Dtype>::Reshape(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
    // Reshape the bottom and top blobs (in the case of a loss layer, the top blob
    // is a scalar)
    LossLayer<Dtype>::Reshape(bottom, top);
    // Check that the first two bottom blobs (bottom[0] corresponds to the output from the net, 
    // bottom[1] corresponds to the label (ground-truth)) are of the same dimension
    CHECK_EQ(bottom[0]->channels(), bottom[1]->channels());
    CHECK_EQ(bottom[0]->height(), bottom[1]->height());
    CHECK_EQ(bottom[0]->width(), bottom[1]->width());
    // Reshape the diff_ blob (blob containing the difference image) according to the dimension of
    // the first bottom blob (bottom[0])
    diff_.Reshape(bottom[0]->num(), bottom[0]->channels(),
                  bottom[0]->height(), bottom[0]->width());
}


// Perform layer-specific setup
template<typename Dtype>
void HeatmapLossLayer<Dtype>::LayerSetUp(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top)
{
    // If loss_weight is not specified for this layer, set it to 1
    if (this->layer_param_.loss_weight_size() == 0) {
        this->layer_param_.add_loss_weight(Dtype(1));
    }

}


// Implement the CPU forward pass
template <typename Dtype>
void HeatmapLossLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
        const vector<Blob<Dtype>*>& top)
{
    // Initialize the loss to 0
    Dtype loss = 0;

    // Get the value of the visualize flag. Specifies whether or not the layer's output has to be visualized.
    bool visualize = this->layer_param_.heatmap_loss_param().visualize();
    // Get the value of the channel to visualize (0, by default)
    int visualize_channel = this->layer_param_.heatmap_loss_param().visualize_channel();

    // Initialize bottom_pred to the blob output by the network (prediction)
    const Dtype* bottom_pred = bottom[0]->cpu_data();
    // Initialize gt_pred to the expected blob (ground-truth)
    const Dtype* gt_pred = bottom[1]->cpu_data();
    // Number of images in the ground-truth (batch size)
    const int num_images = bottom[1]->num();
    // Height and width of images in ground-truth
    const int label_height = bottom[1]->height();
    const int label_width = bottom[1]->width();
    // Number of channels in the blob predicted by the network (one for each image, presumably)
    const int num_channels = bottom[0]->channels();

    // Display the height, width, and number of channels in the network prediction
    DLOG(INFO) << "bottom size: " << bottom[0]->height() << " " << bottom[0]->width() << " " << bottom[0]->channels();

    // Size of each channel in the ground-truth (label)
    const int label_channel_size = label_height * label_width;
    // Size of each image's label (size_per_channel * num_channels)
    const int label_img_size = label_channel_size * num_channels;

    // Initialize OpenCV images for visualization
    cv::Mat bottom_img, gt_img, diff_img;

    // Create windows and images, if they are to be visualized
    if (visualize)
    {
        // // Output from the network
        // cv::namedWindow("bottom", CV_WINDOW_AUTOSIZE);
        // // Ground-truth (label)
        // cv::namedWindow("gt", CV_WINDOW_AUTOSIZE);
        // // Difference between the above (prediction and true label)
        // cv::namedWindow("diff", CV_WINDOW_AUTOSIZE);
        // Overlay image
        cv::namedWindow("overlay", CV_WINDOW_AUTOSIZE);
        // // (???)
        // cv::namedWindow("visualisation_bottom", CV_WINDOW_AUTOSIZE);
        // Image from the bottom blob
        bottom_img = cv::Mat::zeros(label_height, label_width, CV_32FC1);
        // Ground-truth (label)
        gt_img = cv::Mat::zeros(label_height, label_width, CV_32FC1);
        // Difference image
        diff_img = cv::Mat::zeros(label_height, label_width, CV_32FC1);
    }

    // Loop over images (each keypoint, for each image in the batch)
    for (int idx_img = 0; idx_img < num_images; idx_img++)
    {
        // Compute Euclidean loss for each channel
        for (int idx_ch = 0; idx_ch < num_channels; idx_ch++)
        {
            // For each pixel
            for (int i = 0; i < label_height; i++)
            {
                for (int j = 0; j < label_width; j++)
                {
                    // Compute the difference between image pixels and add them
                    int image_idx = idx_img * label_img_size + idx_ch * label_channel_size + i * label_height + j;
                    float diff = (float)bottom_pred[image_idx] - (float)gt_pred[image_idx];
                    loss += diff * diff;

                    // Store visualisation for given channel
                    if (idx_ch == visualize_channel && visualize)
                    {
                        // Intensity value at the pixel in the blob output from the network (predicted heatmap)
                        bottom_img.at<float>((int)j, (int)i) = (float) bottom_pred[image_idx];
                        // Intensity value at the pixel in the ground-truth
                        gt_img.at<float>((int)j, (int)i) = (float) gt_pred[image_idx];
                        // Store the magnitude of the difference
                        diff_img.at<float>((int)j, (int)i) = (float) diff * diff;
                    }

                }
            }
        }

        // Plot visualisation
        if (visualize)
        {
            // DLOG(INFO) << "num_images=" << num_images << " idx_img=" << idx_img;
            // DLOG(INFO) << "sum bottom: " << cv::sum(bottom_img) << "  sum gt: " << cv::sum(gt_img);
            
            // Size of the visualization window
            int visualisation_size = 256;
            cv::Size size(visualisation_size, visualisation_size);
            // Vector to store points
            std::vector<cv::Point> points;
            this->Visualize(loss, bottom_img, gt_img, diff_img, points, size);
            this->VisualizeBottom(bottom, idx_img, visualize_channel, points, size);
            cv::waitKey(0);     // Wait forever a key is pressed
        }
    }

    // Display 'loss' and 'normalized loss'
    DLOG(INFO) << "total loss: " << loss;
    loss /= (num_images * num_channels * label_channel_size);
    DLOG(INFO) << "total normalized loss: " << loss;

    // Generate the top blob that contains the loss
    top[0]->mutable_cpu_data()[0] = loss;
}



// visualize GT heatmap, predicted heatmap, input image, and maxima in heatmap
// bottom_img: predicted heatmap(s)
// gt_img: ground truth gaussian heatmaps
// diff_img: per-pixel loss (squared)
// points: vector to hold points (to be plotted by the calling function)
// size: dimensions of the visualization window
// overlay: prediction with GT location & max of prediction
// visualisation_bottom: additional visualisation layer (defined as the last 'bottom' in the loss prototxt def)
template <typename Dtype>
void HeatmapLossLayer<Dtype>::Visualize(float loss, cv::Mat bottom_img, cv::Mat gt_img, 
    cv::Mat diff_img, std::vector<cv::Point>& points, cv::Size size)
{

    // Display the loss (when in debug mode)
    DLOG(INFO) << loss;

    // Definitions
    double minVal, maxVal;
    cv::Point minLocGT, maxLocGT;
    cv::Point minLocBottom, maxLocBottom;
    cv::Point minLocThird, maxLocThird;
    cv::Mat overlay_img_orig, overlay_img;

    // Convert prediction (bottom) into 3 channels, call 'overlay'
    // This does not work very well (subtracts 1 from the bottom image, so nothing is visible)
    // overlay_img_orig = bottom_img.clone() - 1;
    // We overlay the GT and prediction on the response of bottom, i.e., on the heatmap produced
    // by the network
    overlay_img_orig = bottom_img.clone();
    // Create a 3 channel image using the overlay image
    cv::Mat in[] = {overlay_img_orig, overlay_img_orig, overlay_img_orig};
    cv::merge(in, 3, overlay_img);

    // Resize all images to fixed size (before resizing, convert from Caffe blob to OpenCV format)
    PrepVis(bottom_img, size);
    cv::resize(bottom_img, bottom_img, size);
    PrepVis(gt_img, size);
    cv::resize(gt_img, gt_img, size);
    PrepVis(diff_img, size);
    cv::resize(diff_img, diff_img, size);
    PrepVis(overlay_img, size);
    cv::resize(overlay_img, overlay_img, size);

    // Get and plot GT position & prediction position in new visualisation-resized space
    cv::minMaxLoc(gt_img, &minVal, &maxVal, &minLocGT, &maxLocGT);
    DLOG(INFO) << "gt min: " << minVal << "  max: " << maxVal;
    cv::minMaxLoc(bottom_img, &minVal, &maxVal, &minLocBottom, &maxLocBottom);
    DLOG(INFO) << "bottom min: " << minVal << "  max: " << maxVal;
    // Indicate the true ground-truth location using a green circle
    cv::circle(overlay_img, maxLocGT, 5, cv::Scalar(0, 255, 0), -1);
    // Indicate the predicted location using a red circle
    cv::circle(overlay_img, maxLocBottom, 3, cv::Scalar(0, 0, 255), -1);

    // // Show visualisation images
    // cv::imshow("bottom", bottom_img - 1);
    // cv::imshow("gt", gt_img - 1);
    // cv::imshow("diff", diff_img);
    cv::imshow("overlay", overlay_img - 1);

    // Store max locations
    points.push_back(maxLocGT);
    points.push_back(maxLocBottom);
}


// Plot another visualisation image overlaid with ground truth & prediction locations
// (particularly useful e.g. if you set this to the original input image)
template <typename Dtype>
void HeatmapLossLayer<Dtype>::VisualizeBottom(const vector<Blob<Dtype>*>& bottom, int idx_img, int visualize_channel, std::vector<cv::Point>& points, cv::Size size)
{
    // Determine which layer to visualize (bottom[2] is the actual image (data blob))
    Blob<Dtype>* visualisation_bottom = bottom[2];
    DLOG(INFO) << "visualisation_bottom: " << visualisation_bottom->channels() << " " << visualisation_bottom->height() << " " << visualisation_bottom->width();

    // Format as RGB / gray
    bool isRGB = visualisation_bottom->channels() == 3;
    cv::Mat visualisation_bottom_img;
    if (isRGB){
        visualisation_bottom_img = cv::Mat::zeros(visualisation_bottom->height(), visualisation_bottom->width(), CV_32FC3);
    }
    else{
        visualisation_bottom_img = cv::Mat::zeros(visualisation_bottom->height(), visualisation_bottom->width(), CV_32FC1);
    }

    // Convert frame from Caffe representation to OpenCV image
    for (int idx_ch = 0; idx_ch < visualisation_bottom->channels(); idx_ch++)
    {
        for (int i = 0; i < visualisation_bottom->height(); i++)
        {
            for (int j = 0; j < visualisation_bottom->width(); j++)
            {
                int image_idx = idx_img * visualisation_bottom->width() * visualisation_bottom->height() * visualisation_bottom->channels() + idx_ch * visualisation_bottom->width() * visualisation_bottom->height() + i * visualisation_bottom->height() + j;
                if (isRGB && idx_ch < 3) {
                    visualisation_bottom_img.at<cv::Vec3f>((int)j, (int)i)[idx_ch] = 4 * (float) visualisation_bottom->cpu_data()[image_idx] / 255;
                } else if (idx_ch == visualize_channel)
                {
                    visualisation_bottom_img.at<float>((int)j, (int)i) = (float) visualisation_bottom->cpu_data()[image_idx];
                }
            }
        }
    }

    // Resize all images to fixed size (before resizing, convert them from Caffe to OpenCV format)
    PrepVis(visualisation_bottom_img, size);
    cv::resize(visualisation_bottom_img, visualisation_bottom_img, size);

    // Convert colouring if RGB
    if (isRGB){
        cv::cvtColor(visualisation_bottom_img, visualisation_bottom_img, CV_RGB2BGR);
    }

    // Plot max of GT & prediction
    cv::Point maxLocGT = points[0];
    cv::Point maxLocBottom = points[1];    
    cv::circle(visualisation_bottom_img, maxLocGT, 5, cv::Scalar(0, 255, 0), -1);
    cv::circle(visualisation_bottom_img, maxLocBottom, 3, cv::Scalar(0, 0, 255), -1);

    // Show visualisation
    cv::imshow("visualisation_bottom", visualisation_bottom_img - 1);
}



// Convert from Caffe representation to OpenCV img
template <typename Dtype>
void HeatmapLossLayer<Dtype>::PrepVis(cv::Mat img, cv::Size size)
{
    cv::transpose(img, img);
    cv::flip(img, img, 1);
}


// Implement CPU backward pass
template <typename Dtype>
void HeatmapLossLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
        const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom)
{
    // Number of elements in the first bottom blob (the blob output by the network)
    const int count = bottom[0]->count();
    // Number of channels in the bottom blob
    // Commenting this since it goes unused
    // const int channels = bottom[0]->channels();

    // Subtract 'count' entries from bottom[0] and bottom[1]. Store the result in diff
    caffe_sub(count, bottom[0]->cpu_data(), bottom[1]->cpu_data(), diff_.mutable_cpu_data());

    // Strictly speaking, should be normalising by (2 * channels) due to 1/2 multiplier in front of the loss
    // Commenting it since it is unused
    // Dtype loss = caffe_cpu_dot(count, diff_.cpu_data(), diff_.cpu_data()) / Dtype(channels);

    // Copy the gradients to the bottom blobs (to both of them, in fact)
    memcpy(bottom[0]->mutable_cpu_diff(), diff_.cpu_data(), sizeof(Dtype) * count);
    memcpy(bottom[1]->mutable_cpu_diff(), diff_.cpu_data(), sizeof(Dtype) * count);

}


// Forward GPU pass is not implemented
template <typename Dtype>
void HeatmapLossLayer<Dtype>::Forward_gpu(const vector<Blob<Dtype>*>& bottom,
        const vector<Blob<Dtype>*>& top)
{
    Forward_cpu(bottom, top);
}


// Backward GPU pass is not implemented
template <typename Dtype>
void HeatmapLossLayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
        const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom)
{
    Backward_cpu(top, propagate_down, bottom);
}


// GPU pass is a stub (only CPU pass is implemented)
#ifdef CPU_ONLY
STUB_GPU(HeatmapLossLayer);
#endif

INSTANTIATE_CLASS(HeatmapLossLayer);
REGISTER_LAYER_CLASS(HeatmapLoss);


}  // namespace caffe
