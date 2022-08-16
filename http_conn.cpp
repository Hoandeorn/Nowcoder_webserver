//
// Created by 黄志鸿 on 2022/8/4.
//

#include "http_conn.h"

#define DEBUG

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

const char* doc_root = "/home/sapplehalf/Documents/webserver/resources";

int http_conn::m_epollfd = -1; // all socket events are registed on the same epoll object.
int http_conn::m_user_count = 0; // # of clients.

void setnonblocking(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    old_flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, old_flag);
}

// add fd which are monitored in epoll.
void addfd(int epollfd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // POLLRDHUP: P151
    if (one_shot) {
        event.events |= EPOLLONESHOT; // 防止同一个通信被不同的线程处理 P157
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event); // P152
    setnonblocking(fd); // set unblock of the fd, P157
};

// remove listened fd from epoll
int removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//modify fd, reset oneshot event to make sure that EPOLLIN
// can be triggered at the next time when the fd is readable.
void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void http_conn::init(int sockfd, const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;

    // port multiplexing
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    // add to EPOLL.
    addfd(m_epollfd, m_sockfd, true);
    m_user_count++;
    init();
}

void http_conn::close_conn(){
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

bool http_conn::read(){
    if (m_read_idx > READ_BUFFER_SIZE) return false; // current pointer position is after the end of buffer.
    int bytes_read = 0; // read bytes
    while(true){
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 ); // P81
        if (bytes_read == -1){ // error
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // no data
            return false;
        } else if (bytes_read == 0) {  //connection closed
            return false;
        }
        m_read_idx += bytes_read;
    }
    printf("%s", m_read_buf);
    return true;
}

bool http_conn::write(){
    int temp = 0;
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        init();
        return true;
    }

    while(true) {
        temp = writev(m_sockfd, m_iv, m_iv_count); // read or write data into multiple buffers
        if (temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_have_send >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }
        if (!bytes_to_send) {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger) {
                init();
                return true;
            }
            else return false;
        }

    }

}

// used by working thread in the thread pool.
// The entry function of processing HTTP requests.
// MAIN STATUS MACHINE
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = nullptr;
    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
    || ((line_status = parse_line()) == LINE_OK ))
        // 解析请求体 && 读取完整 || reading and 读取到一个完整的行
        // get a line.
    {
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 line: %s\n", text);

        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) return BAD_REQUEST;
                printf("CHECK_STATE_REQUESTLINE");
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST) return do_request();
                line_status = LINE_OPEN;
                printf("CHECK_STATE_CONTENT");
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if (ret == GET_REQUEST) return do_request();
                else if (ret == BAD_REQUEST) return BAD_REQUEST;
                printf("CHECK_STATE_HEADER");
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    // "GET"
    m_url = strpbrk(text, " \t");
    if (! m_url) return BAD_REQUEST;
    *m_url++ = '\0';
    char *method = text;

    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else return BAD_REQUEST; // grammar error

    // "HTTP/1.1"
    m_version = strpbrk(m_url, " \t");
    if (!m_version) return BAD_REQUEST;
    *m_version++ = '\0';
    if (strcasecmp(m_version, "HTTP/1.1") != 0) return BAD_REQUEST;

    // "http://192.168.110.129:10000/index.html"
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头

#ifdef DEBUG
    printf("method: %s\n", method);
    printf("m_version: %s\n", m_version);
    printf("m_url: %s\n", m_url);
#endif

    return NO_REQUEST; // 请求不完整，需要继续读取客户数据
}

// 解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    if ( text[0] == '\0') {
        if (m_content_length != 0) {
            // If the http request has the message body, read it (length: m _content_length)

#ifdef DEBUG
printf("m_content_length != 0");
#endif

            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
#ifdef DEBUG
        printf("GET_REQUEST");
#endif
        return GET_REQUEST; // get a complete HTTP request.

    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        // Head part of connection. "Connection: keep-alive"
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) m_linger = true;
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        // Content-Length part
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        // Host part
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        printf("Error: Unknown header %s.\n", text);
    }
    return NO_REQUEST;
}

// 判断HTTP请求的消息体是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if (m_read_idx >= m_content_length + m_checked_idx) {
        text[m_content_length] = '\0';
#ifdef DEBUG
        printf("GET_REQUEST");
#endif
        return GET_REQUEST;
    }
#ifdef DEBUG
    printf("NO_REQUEST");
#endif
    return NO_REQUEST;
}

// 解析一行，判断依据\r\n
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for(;  m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if (m_checked_idx + 1 == m_read_idx) return LINE_OPEN; // incomplete line
            else if (m_read_buf[m_checked_idx + 1] == '\n') { // complete line
                m_read_buf[m_checked_idx++] = '\0'; // add '\0' on '\r'
                m_read_buf[m_checked_idx++] = '\0'; // add '\0' on '\n'
                return LINE_OK;
            } else return LINE_BAD;
        } else if (temp == '\n') {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx -1] == '\r')) {
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process(){
    // parse HTTP requests.
    HTTP_CODE read_ret = process_read();
#ifdef DEBUG
    printf("Finish Reading.\n");
#endif
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
#ifdef DEBUG
        printf("NO_REQUEST.\n");
#endif
        return;
    }

    // create responses.
    bool write_ret = process_write(read_ret);
    if (!write_ret) close_conn();
    modfd(m_epollfd, m_sockfd, EPOLLOUT);

};

void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE; //initialize the state at the first line.

    m_method = GET; // 默认请求方式为GET
    m_url = nullptr;
    m_version = nullptr;
    m_content_length = 0;
    m_host = nullptr;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_linger = false; // 默认不保持链接  Connection : keep-alive保持连接

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);

}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request(){
    strcpy( m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

#ifdef DEBUG
    printf("%s", m_real_file);
#endif

    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if (stat(m_real_file, &m_file_stat) < 0) return NO_RESOURCE; // 0 is success.
    if (!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST; // forbidden access
    if (S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST; //if is directory.
    int fd = open(m_real_file, O_RDONLY); // read only
#ifdef DEBUG
    printf("fd: %d.\n", fd);
#endif
    // create a memory mapping
    m_file_address = (char*) mmap(nullptr, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = nullptr;
    }
}

bool http_conn::process_write(HTTP_CODE ret) {
    switch(ret) {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
#ifdef DEBUG
    printf("FILE_REQUEST.\n");
#endif
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
#ifdef DEBUG
            printf("FILE_REQUEST: true.\n");
#endif
            return true;
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response(const char* format, ...){
    // va_list: https://blog.csdn.net/mediatec/article/details/94637013
    if (m_write_idx >= WRITE_BUFFER_SIZE) return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx,
                        WRITE_BUFFER_SIZE - 1 - m_checked_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_checked_idx)) return false;
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", m_linger ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}