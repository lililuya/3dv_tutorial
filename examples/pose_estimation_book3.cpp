#include "opencv2/opencv.hpp"

int main()
{
    const char *video_file = "../data/blais.mp4", *cover_file = "../data/blais.jpg";
    size_t min_inlier_num = 100;

    // Load the object image and extract features
    cv::Mat obj_image = cv::imread(cover_file);
    if (obj_image.empty()) return -1;

    cv::Ptr<cv::FeatureDetector> fdetector = cv::ORB::create();
    cv::Ptr<cv::DescriptorMatcher> fmatcher = cv::DescriptorMatcher::create("BruteForce-Hamming");
    std::vector<cv::KeyPoint> obj_keypoint;
    cv::Mat obj_descriptor;
    fdetector->detectAndCompute(obj_image, cv::Mat(), obj_keypoint, obj_descriptor);
    if (obj_keypoint.empty() || obj_descriptor.empty()) return -1;
    fmatcher->add(obj_descriptor);

    // Open a video
    cv::VideoCapture video;
    if (!video.open(video_file)) return -1;

    // Prepare a box for simple AR
    std::vector<cv::Point3f> box_lower = { cv::Point3f(30, 145,   0), cv::Point3f(30, 200,   0), cv::Point3f(200, 200,   0), cv::Point3f(200, 145,   0) };
    std::vector<cv::Point3f> box_upper = { cv::Point3f(30, 145, -50), cv::Point3f(30, 200, -50), cv::Point3f(200, 200, -50), cv::Point3f(200, 145, -50) };

    // Run pose estimation and camera calibration together
    while (true)
    {
        // Read an image from the video
        cv::Mat img;
        video >> img;
        if (img.empty()) break;

        // Extract features and match them to the object features
        std::vector<cv::KeyPoint> img_keypoint;
        cv::Mat img_descriptor;
        fdetector->detectAndCompute(img, cv::Mat(), img_keypoint, img_descriptor);
        if (img_keypoint.empty() || img_descriptor.empty()) continue;
        std::vector<cv::DMatch> match;
        fmatcher->match(img_descriptor, match);
        if (match.size() < min_inlier_num) continue;
        std::vector<cv::Point3f> obj_points;
        std::vector<cv::Point2f> obj_project, img_points;
        for (auto m = match.begin(); m < match.end(); m++)
        {
            obj_points.push_back(cv::Point3f(obj_keypoint[m->trainIdx].pt));
            obj_project.push_back(obj_keypoint[m->trainIdx].pt);
            img_points.push_back(img_keypoint[m->queryIdx].pt);
        }

        // Determine whether each matched feature is an inlier or not
        cv::Mat inlier_mask = cv::Mat::zeros(int(match.size()), 1, CV_8U);
        cv::Mat H = cv::findHomography(img_points, obj_project, inlier_mask, cv::RANSAC, 2);
        cv::Mat image_result;
        cv::drawMatches(img, img_keypoint, obj_image, obj_keypoint, match, image_result, cv::Vec3b(0, 0, 255), cv::Vec3b(0, 127, 0), inlier_mask);

        // Check whether inliers are enough or not
        double f = 0;
        size_t inlier_num = static_cast<size_t>(cv::sum(inlier_mask)[0]);
        if (inlier_num > min_inlier_num)
        {
            // Calibrate the camera and estimate its pose with inliers
            std::vector<cv::Point3f> obj_inlier;
            std::vector<cv::Point2f> img_inlier;
            for (int idx = 0; idx < inlier_mask.rows; idx++)
            {
                if (inlier_mask.at<uchar>(idx))
                {
                    obj_inlier.push_back(obj_points[idx]);
                    img_inlier.push_back(img_points[idx]);
                }
            }
            cv::Mat K, dist_coeff;
            std::vector<cv::Mat> rvecs, tvecs;
            cv::calibrateCamera(std::vector<std::vector<cv::Point3f>>(1, obj_inlier), std::vector<std::vector<cv::Point2f>>(1, img_inlier), img.size(), K, dist_coeff, rvecs, tvecs,
                cv::CALIB_FIX_ASPECT_RATIO | cv::CALIB_FIX_PRINCIPAL_POINT | cv::CALIB_ZERO_TANGENT_DIST | cv::CALIB_FIX_K1 | cv::CALIB_FIX_K2 | cv::CALIB_FIX_K3 | cv::CALIB_FIX_K4 | cv::CALIB_FIX_K5 | cv::CALIB_FIX_K6 | cv::CALIB_FIX_S1_S2_S3_S4 | cv::CALIB_FIX_TAUX_TAUY);
            cv::Mat rvec = rvecs[0].clone();
            cv::Mat tvec = tvecs[0].clone();
            f = K.at<double>(0);

            // Draw the box on the image
            cv::Mat line_lower, line_upper;
            cv::projectPoints(box_lower, rvec, tvec, K, dist_coeff, line_lower);
            cv::projectPoints(box_upper, rvec, tvec, K, dist_coeff, line_upper);
            line_lower.reshape(1).convertTo(line_lower, CV_32S); // Change 4 x 1 matrix (CV_64FC2) to 4 x 2 matrix (CV_32SC1)
            line_upper.reshape(1).convertTo(line_upper, CV_32S); //  because 'cv::polylines()' only accepts 'CV_32S' depth.
            cv::polylines(image_result, line_lower, true, cv::Vec3b(255, 0, 0), 2);
            for (int i = 0; i < line_lower.rows; i++)
                cv::line(image_result, cv::Point(line_lower.row(i)), cv::Point(line_upper.row(i)), cv::Vec3b(0, 255, 0), 2);
            cv::polylines(image_result, line_upper, true, cv::Vec3b(0, 0, 255), 2);
        }

        // Show the image and process the key event
        cv::String info = cv::format("Inliers: %d (%d%%), Focal Length: %.0f", inlier_num, 100 * inlier_num / match.size(), f);
        cv::putText(image_result, info, cv::Point(5, 15), cv::FONT_HERSHEY_PLAIN, 1, cv::Vec3b(0, 255, 0));
        cv::imshow("Pose Estimation (Book)", image_result);
        int key = cv::waitKey(1);
        if (key == 32) key = cv::waitKey(); // Space
        if (key == 27) break;               // ESC
    }

    video.release();
    return 0;
}
