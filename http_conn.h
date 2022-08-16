//
// Created by 黄志鸿 on 2022/8/4.
//

#ifndef WEBSERVER_HTTP_CONN_H
#define WEBSERVER_HTTP_CONN_H

#include <sys/epoll.h>
#include <unistd.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <cstdio>
#include <cerrno>
#include <string.h>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cstdarg>
#include <sys/uio.h>



class http_conn {
public:
    static const int READ_BUFFER_SIZE = 16384;
    static const int WRITE_BUFFER_SIZE = 16384;
    static const int FILENAME_LEN = 400;


    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_conn() {};
    ~http_conn() {};

public:
    void init(int sockfd, const sockaddr_in &addr);
    void close_conn();
    void process();
    bool read();
    bool write();

private:
    void init();
    HTTP_CODE process_read(); // analyze HTTP request
    bool process_write(HTTP_CODE ret); // fill HTTP response

    // 下面这一组函数被process_read调用以分析HTTP请求
    HTTP_CODE parse_request_line(char* text); // analyze the request line
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    inline char * get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line(); // get one line by \r\n.

    // 这一组函数被process_write调用以填充HTTP应答。
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_content_type();
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_len);
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd; // 所有socket上的事件都被注册到同一个epoll内核事件中.
    static int m_user_count; // # of clients.

private:
    int m_sockfd; // the socket connected with this HTTP.
    sockaddr_in m_address; // IP

    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    int m_checked_idx; // the position of the character under analyzing in the buffer.
    int m_start_line; // the beginning position of the line under analyzing

    CHECK_STATE m_check_state; // the current status of tbe main status machine.
    METHOD m_method; // request method

    char m_real_file[FILENAME_LEN]; // == doc_root (root address of the website) + m_url
    char *m_url; // object file name;
    char *m_version; // version of the protocol
    char *m_host; // name of host
    bool m_linger; // HTTP request keeps the connection or not
    int m_content_length; // the length of the HTTP request message

    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    char *m_file_address; // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat; // status of the target file
    struct iovec m_iv[2]; // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;

    int bytes_to_send = 0;
    int bytes_have_send = 0;
};

#endif //WEBSERVER_HTTP_CONN_H
