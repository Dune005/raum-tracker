<?php
/**
 * Response-Helper für einheitliche JSON-Antworten
 * Raum-Tracker API
 */

class Response {
    
    /**
     * Sendet JSON-Response
     * 
     * @param int $statusCode HTTP-Status-Code
     * @param array $data Response-Daten
     */
    public static function send($statusCode, $data) {
        http_response_code($statusCode);
        header('Content-Type: application/json; charset=utf-8');
        echo json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
        exit;
    }

    /**
     * Erfolgreiche Response (200)
     */
    public static function success($data = [], $message = null) {
        $response = ['success' => true];
        
        if ($message) {
            $response['message'] = $message;
        }
        
        if (!empty($data)) {
            $response['data'] = $data;
        }
        
        self::send(200, $response);
    }

    /**
     * Created Response (201)
     */
    public static function created($data = [], $message = null) {
        $response = ['success' => true];
        
        if ($message) {
            $response['message'] = $message;
        }
        
        if (!empty($data)) {
            $response = array_merge($response, $data);
        }
        
        self::send(201, $response);
    }

    /**
     * Bad Request (400)
     */
    public static function badRequest($details = null) {
        $response = [
            'success' => false,
            'error' => 'Invalid request'
        ];
        
        if ($details) {
            $response['details'] = $details;
        }
        
        self::send(400, $response);
    }

    /**
     * Unauthorized (401)
     */
    public static function unauthorized($details = 'Invalid or missing API key') {
        $response = [
            'success' => false,
            'error' => 'Unauthorized',
            'details' => $details
        ];
        
        self::send(401, $response);
    }

    /**
     * Not Found (404)
     */
    public static function notFound($details = null) {
        $response = [
            'success' => false,
            'error' => 'Not found'
        ];
        
        if ($details) {
            $response['details'] = $details;
        }
        
        self::send(404, $response);
    }

    /**
     * Method Not Allowed (405)
     */
    public static function methodNotAllowed($allowedMethods = []) {
        $response = [
            'success' => false,
            'error' => 'Method not allowed'
        ];
        
        if (!empty($allowedMethods)) {
            $response['allowed_methods'] = $allowedMethods;
        }
        
        self::send(405, $response);
    }

    /**
     * Internal Server Error (500)
     */
    public static function serverError($details = 'Internal server error') {
        $response = [
            'success' => false,
            'error' => 'Internal server error',
            'details' => $details
        ];
        
        self::send(500, $response);
    }
}
